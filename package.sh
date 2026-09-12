#!/bin/bash

set -euo pipefail

pwd=$(pwd)
echo '当前目录：'
echo $pwd

echo '编译插件'
xmake

echo '编译Go服务器'
cd $pwd/go_server
./build.sh
cd $pwd
echo '----------------'

# 临时文件
mkdir bili_plugin
cp go_server/server ./bili_plugin
cp go_server/bili-sms ./bili_plugin

# 查找编译输出的 libbili_plugin.so
# 按优先级查找：arm64-v8a > aarch64 > x86_64
SO_PATH=""
for candidate in \
    build/linux/arm64-v8a/release/libbili_plugin.so \
    build/linux/arm64-v8a/releasedbg/libbili_plugin.so \
    build/linux/arm64-v8a/debug/libbili_plugin.so \
    build/linux/x86_64/release/libbili_plugin.so \
    build/linux/x86_64/releasedbg/libbili_plugin.so \
    build/aarch64/libbili_plugin.so; do
    if [ -f "$candidate" ]; then
        SO_PATH="$candidate"
        break
    fi
done

if [ -z "$SO_PATH" ]; then
    echo "ERROR: 未找到 libbili_plugin.so"
    echo "查找路径："
    find build -name 'libbili_plugin.so' 2>/dev/null || true
    exit 1
fi

echo "使用: $SO_PATH"
file "$SO_PATH"
cp "$SO_PATH" ./bili_plugin

cp -r ./qml ./bili_plugin
cp metadata.json ./bili_plugin
cp icon.png ./bili_plugin

# 打包
zip -r bili_plugin.zip bili_plugin/*

# 清除
rm -r ./bili_plugin

echo '----------------'
echo '打包完成'
