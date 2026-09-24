package main

import (
	"io"
	"net/http"
	"net/url"
	"strings"
	"time"
)

// isBiliMediaURL 判断是否属于可代理的 B 站媒体（播放/下载 CDN）地址。
func isBiliMediaURL(raw string) bool {
	if !strings.HasPrefix(raw, "http://") && !strings.HasPrefix(raw, "https://") {
		return false
	}
	u, err := url.Parse(raw)
	if err != nil || u.Host == "" {
		return false
	}
	h := strings.ToLower(u.Hostname())
	return strings.Contains(h, "bilivideo") ||
		strings.Contains(h, "hdslb") ||
		strings.Contains(h, "bilibili") ||
		strings.Contains(h, "akamaized")
}

// mediaURLKeys 是需要改写为本地代理的媒体地址字段名。
var mediaURLKeys = map[string]bool{
	"url": true, "baseUrl": true, "base_url": true,
	"backupUrl": true, "backup_url": true,
}

// localProxyURL 把远端媒体地址改写为经本机服务器转发（由服务器补 Referer，规避 CDN 403）。
func localProxyURL(r *http.Request, raw string) string {
	return "http://" + r.Host + "/video/proxy?url=" + url.QueryEscape(raw)
}

// rewriteMediaURLs 递归遍历 JSON 结构，把媒体地址字段改写为本地代理地址。
func rewriteMediaURLs(r *http.Request, v interface{}) {
	switch t := v.(type) {
	case map[string]interface{}:
		for k, val := range t {
			if s, ok := val.(string); ok && mediaURLKeys[k] && isBiliMediaURL(s) {
				t[k] = localProxyURL(r, s)
				continue
			}
			rewriteMediaURLs(r, val)
		}
	case []interface{}:
		for _, val := range t {
			rewriteMediaURLs(r, val)
		}
	}
}

// streamingClient 用于转发媒体流：不设整体超时，交给请求上下文控制。
var streamingClient = &http.Client{
	Transport: &http.Transport{
		Proxy:                 http.ProxyFromEnvironment,
		MaxIdleConns:          16,
		IdleConnTimeout:       90 * time.Second,
		ForceAttemptHTTP2:     true,
		ResponseHeaderTimeout: 20 * time.Second,
	},
}

// handleVideoProxy: GET /video/proxy?url=<encoded>
// 以带 Referer/UA 的方式请求 B 站 CDN 并原样转发（支持 Range）。
func handleVideoProxy(w http.ResponseWriter, r *http.Request) {
	raw := r.URL.Query().Get("url")
	if raw == "" {
		writeError(w, 400, "url 为必填参数")
		return
	}
	if !isBiliMediaURL(raw) {
		writeError(w, 400, "仅允许代理 B 站媒体地址")
		return
	}

	req, err := http.NewRequestWithContext(r.Context(), http.MethodGet, raw, nil)
	if err != nil {
		writeError(w, 400, err.Error())
		return
	}
	req.Header.Set("User-Agent", defaultUserAgent)
	req.Header.Set("Referer", defaultReferer)
	if rg := r.Header.Get("Range"); rg != "" {
		req.Header.Set("Range", rg)
	}
	if ir := r.Header.Get("If-Range"); ir != "" {
		req.Header.Set("If-Range", ir)
	}

	resp, err := streamingClient.Do(req)
	if err != nil {
		logError("代理媒体失败: %s", err.Error())
		writeError(w, 502, err.Error())
		return
	}
	defer resp.Body.Close()

	for _, h := range []string{
		"Content-Type", "Content-Length", "Content-Range", "Accept-Ranges",
		"Content-Disposition", "Last-Modified", "ETag", "Cache-Control",
	} {
		if v := resp.Header.Get(h); v != "" {
			w.Header().Set(h, v)
		}
	}

	// 媒体流式转发可能远超服务器默认 WriteTimeout，清掉本次写超时
	_ = http.NewResponseController(w).SetWriteDeadline(time.Time{})

	w.WriteHeader(resp.StatusCode)
	if _, err := io.Copy(w, resp.Body); err != nil {
		logWarn("代理媒体中断: %s", err.Error())
	}
}
