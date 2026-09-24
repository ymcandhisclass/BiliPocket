package main

import (
	"io"
	"net/http"
	"net/url"
	"strings"
	"time"
)

// isHTTPURL 判断是否为可代理的 http(s) 地址。
func isHTTPURL(raw string) bool {
	return strings.HasPrefix(raw, "http://") || strings.HasPrefix(raw, "https://")
}

// isLocalURL 判断是否已经是指向本机的地址（无需再代理）。
func isLocalURL(raw string) bool {
	u, err := url.Parse(raw)
	if err != nil {
		return false
	}
	h := strings.ToLower(u.Hostname())
	return h == "127.0.0.1" || h == "localhost" || h == "::1"
}

// shouldProxyURL：媒体字段里除本机地址外的所有 http(s) 地址都走代理。
// （B 站会返回各种 CDN/PCDN 域名，无法穷举白名单，统一由服务器补 Referer。）
func shouldProxyURL(raw string) bool {
	return isHTTPURL(raw) && !isLocalURL(raw)
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
			if mediaURLKeys[k] {
				rewriteMediaValue(r, t, k, val)
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

// rewriteMediaValue 处理媒体字段的值：字符串或字符串数组。
func rewriteMediaValue(r *http.Request, m map[string]interface{}, key string, v interface{}) {
	switch t := v.(type) {
	case string:
		if shouldProxyURL(t) {
			m[key] = localProxyURL(r, t)
		}
	case []interface{}:
		for i, e := range t {
			if s, ok := e.(string); ok && shouldProxyURL(s) {
				t[i] = localProxyURL(r, s)
			}
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
	if !isHTTPURL(raw) {
		writeError(w, 400, "仅允许 http(s) 媒体地址")
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
