#!/usr/bin/env python3
"""Query AMap IP location first, then query live weather by adcode."""

from __future__ import annotations

import json
import os
import socket
import sys
from typing import Any, TextIO
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen


AMAP_KEY = "47478abb752cf12e1a6c91ab0892cae0"
IP_LOCATION_URL = "https://restapi.amap.com/v3/ip"
WEATHER_URL = "https://restapi.amap.com/v3/weather/weatherInfo"
REQUEST_TIMEOUT_SECONDS = 10
DEFAULT_CITY_NAME = "广州"
DEFAULT_ADCODE = "440100"

ANSI_RESET = "\033[0m"
ANSI_BOLD = "\033[1m"
ANSI_KEY = "\033[96m"
ANSI_STRING = "\033[92m"
ANSI_NUMBER = "\033[93m"
ANSI_LITERAL = "\033[95m"
ANSI_NULL = "\033[90m"
ANSI_WARNING = "\033[93m"
ANSI_ERROR = "\033[91m"

JsonValue = dict[str, Any] | list[Any] | str | int | float | bool | None


class AMapRequestError(RuntimeError):
    """Raised when an AMap request cannot produce a usable response."""


def _supports_color(stream: TextIO) -> bool:
    """Return whether ANSI colors should be written to the stream."""
    return (
        hasattr(stream, "isatty")
        and stream.isatty()
        and os.environ.get("TERM") != "dumb"
        and "NO_COLOR" not in os.environ
    )


def _color(text: str, ansi_code: str, enabled: bool) -> str:
    if not enabled:
        return text
    return f"{ansi_code}{text}{ANSI_RESET}"


def _json_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def format_json(value: JsonValue, *, indent: int = 0, use_color: bool = True) -> str:
    """Recursively format a JSON-compatible value with optional ANSI highlighting."""
    padding = " " * indent
    child_padding = " " * (indent + 2)

    if isinstance(value, dict):
        if not value:
            return "{}"

        lines: list[str] = ["{"]
        items = list(value.items())
        for index, (key, child) in enumerate(items):
            key_text = _color(_json_string(str(key)), ANSI_KEY, use_color)
            child_text = format_json(child, indent=indent + 2, use_color=use_color)
            comma = "," if index < len(items) - 1 else ""
            lines.append(f"{child_padding}{key_text}: {child_text}{comma}")
        lines.append(f"{padding}}}")
        return "\n".join(lines)

    if isinstance(value, list):
        if not value:
            return "[]"

        lines = ["["]
        for index, child in enumerate(value):
            child_text = format_json(child, indent=indent + 2, use_color=use_color)
            comma = "," if index < len(value) - 1 else ""
            lines.append(f"{child_padding}{child_text}{comma}")
        lines.append(f"{padding}]")
        return "\n".join(lines)

    if isinstance(value, str):
        return _color(_json_string(value), ANSI_STRING, use_color)
    if isinstance(value, bool):
        return _color("true" if value else "false", ANSI_LITERAL, use_color)
    if value is None:
        return _color("null", ANSI_NULL, use_color)
    if isinstance(value, (int, float)):
        return _color(json.dumps(value, allow_nan=False), ANSI_NUMBER, use_color)

    raise TypeError(f"不支持的 JSON 类型：{type(value).__name__}")


def print_json(title: str, payload: JsonValue) -> None:
    use_color = _supports_color(sys.stdout)
    print(_color(f"\n=== {title} ===", ANSI_BOLD, use_color))
    print(format_json(payload, use_color=use_color))


def print_warning(message: str) -> None:
    use_color = _supports_color(sys.stderr)
    print(_color(f"警告：{message}", ANSI_WARNING, use_color), file=sys.stderr)


def request_json(url: str, params: dict[str, str]) -> JsonValue:
    """Send an HTTPS GET request and return its decoded JSON response."""
    request_url = f"{url}?{urlencode(params)}"
    request = Request(
        request_url,
        headers={
            "Accept": "application/json",
            "User-Agent": "amap-weather-python/1.0",
        },
        method="GET",
    )

    try:
        with urlopen(request, timeout=REQUEST_TIMEOUT_SECONDS) as response:
            charset = response.headers.get_content_charset() or "utf-8"
            body = response.read().decode(charset)
    except HTTPError as exc:
        raise AMapRequestError(f"HTTP 请求失败：{exc.code} {exc.reason}") from exc
    except (socket.timeout, TimeoutError) as exc:
        raise AMapRequestError(f"请求超时（{REQUEST_TIMEOUT_SECONDS} 秒）") from exc
    except URLError as exc:
        reason = exc.reason if exc.reason else "未知网络错误"
        raise AMapRequestError(f"网络请求失败：{reason}") from exc
    except UnicodeDecodeError as exc:
        raise AMapRequestError(f"响应文本解码失败：{exc}") from exc

    try:
        return json.loads(body)
    except json.JSONDecodeError as exc:
        raise AMapRequestError(
            f"响应不是有效 JSON：第 {exc.lineno} 行，第 {exc.colno} 列"
        ) from exc


def require_success(payload: JsonValue, api_name: str) -> dict[str, Any]:
    """Validate the common AMap status fields and return the object response."""
    if not isinstance(payload, dict):
        raise AMapRequestError(f"{api_name}响应的顶层结构不是 JSON 对象")

    if payload.get("status") != "1":
        info = payload.get("info", "未知错误")
        infocode = payload.get("infocode", "未知状态码")
        raise AMapRequestError(f"{api_name}失败：{info}（infocode={infocode}）")

    return payload


def query_ip_location() -> dict[str, Any]:
    payload = request_json(IP_LOCATION_URL, {"key": AMAP_KEY})
    print_json("高德 IP 定位响应", payload)
    return require_success(payload, "IP 定位")


def extract_adcode(payload: dict[str, Any]) -> str:
    adcode = payload.get("adcode")
    if not isinstance(adcode, str) or not adcode.strip():
        raise AMapRequestError("IP 定位成功，但响应中没有可用的 adcode，无法查询天气")
    return adcode.strip()


def query_live_weather(adcode: str) -> dict[str, Any]:
    payload = request_json(
        WEATHER_URL,
        {
            "key": AMAP_KEY,
            "city": adcode,
            "extensions": "base",
        },
    )
    print_json(f"高德实况天气响应（adcode={adcode}）", payload)
    weather = require_success(payload, "天气查询")

    lives = weather.get("lives")
    if not isinstance(lives, list) or not lives:
        raise AMapRequestError("天气查询成功，但响应中的 lives 为空")

    return weather


def main() -> int:
    try:
        try:
            location = query_ip_location()
            adcode = extract_adcode(location)
        except AMapRequestError as exc:
            print_warning(
                f"IP 定位不可用：{exc}；"
                f"改为查询默认城市{DEFAULT_CITY_NAME}（adcode={DEFAULT_ADCODE}）的天气"
            )
            adcode = DEFAULT_ADCODE

        query_live_weather(adcode)
    except AMapRequestError as exc:
        use_color = _supports_color(sys.stderr)
        print(_color(f"错误：{exc}", ANSI_ERROR, use_color), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
