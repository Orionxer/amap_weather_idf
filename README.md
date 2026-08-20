# ESP-IDF 高德实况天气

[![Firmware Release](https://github.com/Orionxer/amap_weather_idf/actions/workflows/firmware-release.yml/badge.svg)](https://github.com/Orionxer/amap_weather_idf/actions/workflows/firmware-release.yml)

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
  SSID `VTK`、密码 `AA12345678@` 和 WPA2 扫描认证阈值。
- 在 `AMap Weather Configuration` 中配置 `AMap Web Service API Key`。
  工程已按 `amap_weather.py` 提供的 Key 设置默认值。

工程按开发板实际容量将 Flash 配置为 16MB，并在连接前将 Wi-Fi 驱动日志
限制为 Error；高德请求及应用日志仍保持默认 Info 级别。

当前工程目标由 `sdkconfig` 中的 `CONFIG_IDF_TARGET` 确定。

## 自动构建与固件下载

GitHub Actions 会在 `main` 分支每次推送后使用 ESP-IDF 5.5.3 构建 ESP32-C5
固件，并更新 [`latest` 预发布版本](https://github.com/Orionxer/amap_weather_idf/releases/tag/latest)。
Pull Request 和手动触发只执行构建，产物保留在对应的 Actions 运行记录中。

Release 提供以下文件：

- `amap_weather-esp32c5-merged.bin`：包含 bootloader、分区表和应用程序，
  从地址 `0x0` 烧录。
- `amap_weather-esp32c5.zip`：包含各个独立固件文件、`flash_args` 和
  `flasher_args.json`。
- `SHA256SUMS`：Release 固件的 SHA-256 校验值。

下载合并固件后可执行：

```sh
esptool.py --chip esp32c5 -p PORT write_flash 0x0 amap_weather-esp32c5-merged.bin
```

将 `PORT` 替换为开发板串口。若使用 ZIP 中的独立固件，解压并进入
`amap_weather-esp32c5` 目录后执行：

```sh
esptool.py --chip esp32c5 -p PORT write_flash @flash_args
```

## 请求流程

1. 请求 `https://restapi.amap.com/v3/ip`。
2. 校验高德公共字段 `status`，并读取非空 `adcode`。
3. 定位失败时使用默认 `adcode=440100`。
4. 请求 `https://restapi.amap.com/v3/weather/weatherInfo`，参数
   `extensions=base`。
5. 校验 `status` 和非空 `lives` 数组，原样记录高德 JSON 响应。
