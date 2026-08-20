# ESP-IDF 高德实况天气

工程启动后连接网络，先调用高德 IP 定位接口获取 `adcode`，再使用该
`adcode` 调用实况天气接口。IP 定位请求或响应不可用时，改为查询广州
（`adcode=440100`）。

HTTPS 请求使用 ESP-IDF 内置的 CA 证书包校验高德服务器证书，不需要嵌入
高德站点专属证书。接口响应和运行状态使用标准 `ESP_LOGI`、`ESP_LOGW` 和
`ESP_LOGE` 输出，不进行 JSON 美化或 ANSI 高亮。

工程关闭了 mbedTLS 的证书有效期时间校验，因此 HTTPS 请求不依赖 SNTP
校时。通用 CA 证书包仍用于验证服务器证书链和域名。

## 配置

```sh
idf.py menuconfig
```

- 在 `Example Connection Configuration` 中配置 Wi-Fi。工程默认使用
  SSID `VTK` 和密码 `AA12345678@`。
- 在 `AMap Weather Configuration` 中配置 `AMap Web Service API Key`。
  工程已按 `amap_weather.py` 提供的 Key 设置默认值。

当前工程目标由 `sdkconfig` 中的 `CONFIG_IDF_TARGET` 确定。

## 请求流程

1. 请求 `https://restapi.amap.com/v3/ip`。
2. 校验高德公共字段 `status`，并读取非空 `adcode`。
3. 定位失败时使用默认 `adcode=440100`。
4. 请求 `https://restapi.amap.com/v3/weather/weatherInfo`，参数
   `extensions=base`。
5. 校验 `status` 和非空 `lives` 数组，原样记录高德 JSON 响应。
