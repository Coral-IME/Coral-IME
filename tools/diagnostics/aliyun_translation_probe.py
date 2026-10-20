"""Standalone TranslateGeneral probe. Python standard library only.

Credentials are entered locally or read from ALIBABA_CLOUD_ACCESS_KEY_ID /
ALIBABA_CLOUD_ACCESS_KEY_SECRET. They are never saved in the report.
Protocol references and usage: docs/aliyun_translation_probe.md.
"""

import argparse
from dataclasses import dataclass, field
from datetime import datetime, timezone
import getpass
import hashlib
import hmac
import json
import os
from pathlib import Path
import queue
import re
import ssl
import threading
import time
from urllib import error, parse, request
import uuid


ENDPOINT = "https://mt.aliyuncs.com/"
VERSION = "2018-10-12"
ALGORITHM = "ACS3-HMAC-SHA256"
MAX_RESPONSE_BYTES = 1024 * 1024


@dataclass(frozen=True)
class Credentials:
    access_key_id: str = field(repr=False)
    access_key_secret: str = field(repr=False)
    security_token: str = field(default="", repr=False)

    def validate(self):
        if not self.access_key_id.strip() or not self.access_key_secret.strip():
            raise ProbeError("MissingCredentials", "请填写 AccessKey ID 和 AccessKey Secret。")
        for value in (self.access_key_id, self.access_key_secret, self.security_token):
            if "\r" in value or "\n" in value:
                raise ProbeError("InvalidCredentials", "密钥或 Token 中不能包含换行。")

    def redact(self, value):
        for secret in (self.access_key_secret, self.security_token, self.access_key_id):
            if secret:
                value = value.replace(secret, "[REDACTED]")
        return value


class ProbeError(Exception):
    def __init__(self, code, message, request_id="", http_status=None):
        super().__init__(message)
        self.code = code
        self.request_id = request_id
        self.http_status = http_status

    def as_dict(self):
        return {"ok": False, "code": self.code, "message": str(self),
                "request_id": self.request_id, "http_status": self.http_status}


def sign_headers(method, host, action, version, body, credentials, *,
                 query="", timestamp=None, nonce=None, content_type=None):
    """ACS3 canonicalization; no credential-bearing diagnostic output."""
    headers = {
        "host": host,
        "x-acs-action": action,
        "x-acs-version": version,
        "x-acs-date": timestamp or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "x-acs-signature-nonce": nonce or uuid.uuid4().hex,
        "x-acs-content-sha256": hashlib.sha256(body).hexdigest(),
    }
    if content_type:
        headers["content-type"] = content_type
    if credentials.security_token:
        headers["x-acs-security-token"] = credentials.security_token
    signed_names = sorted(headers)
    canonical_headers = "".join(f"{key}:{headers[key].strip()}\n" for key in signed_names)
    signed_headers = ";".join(signed_names)
    canonical = "\n".join((method, "/", query, canonical_headers,
                           signed_headers, headers["x-acs-content-sha256"]))
    string_to_sign = ALGORITHM + "\n" + hashlib.sha256(canonical.encode("utf-8")).hexdigest()
    signature = hmac.new(credentials.access_key_secret.encode("utf-8"),
                         string_to_sign.encode("utf-8"), hashlib.sha256).hexdigest()
    headers["authorization"] = (
        f"{ALGORITHM} Credential={credentials.access_key_id},"
        f"SignedHeaders={signed_headers},Signature={signature}")
    return headers


def build_request(text, source, target, credentials):
    if not text.strip() or len(text) > 5000:
        raise ProbeError("InvalidText", "原文不能为空，且不能超过 5000 字符。")
    if not re.fullmatch(r"[a-z][a-z0-9-]{0,31}", source) or not re.fullmatch(r"[a-z][a-z0-9-]{0,31}", target):
        raise ProbeError("InvalidLanguage", "请输入语言代码，例如 auto、zh、en。")
    if target == "auto":
        raise ProbeError("InvalidLanguage", "目标语言不能为 auto。")
    credentials.validate()
    body = parse.urlencode({"FormatType": "text", "SourceLanguage": source,
                            "TargetLanguage": target, "SourceText": text,
                            "Scene": "general"}, quote_via=parse.quote, safe="~").encode("utf-8")
    headers = sign_headers("POST", "mt.aliyuncs.com", "TranslateGeneral", VERSION,
                           body, credentials, content_type="application/x-www-form-urlencoded")
    headers["accept"] = "application/json"
    headers["user-agent"] = "CoralPinyin-TranslationProbe/1"
    return request.Request(ENDPOINT, data=body, headers=headers, method="POST")


class NoRedirect(request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ProbeError("RedirectRejected", "接口发生重定向，已停止请求。", http_status=code)


def read_response(response):
    raw = response.read(MAX_RESPONSE_BYTES + 1)
    if len(raw) > MAX_RESPONSE_BYTES:
        raise ProbeError("ResponseTooLarge", "接口返回超过 1 MiB。")
    try:
        payload = json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeError):
        raise ProbeError("InvalidResponse", "接口未返回有效 UTF-8 JSON。") from None
    if not isinstance(payload, dict):
        raise ProbeError("InvalidResponse", "接口返回的 JSON 不是对象。")
    return payload


def parse_result(payload, http_status, elapsed_ms, credentials):
    request_id = str(payload.get("RequestId", ""))[:128]
    code = payload.get("Code")
    if http_status != 200 or str(code) != "200":
        raw_message = str(payload.get("Message", "阿里云返回调用失败。"))
        raise ProbeError(credentials.redact(str(code or "HttpError")),
                         credentials.redact(raw_message)[:1000],
                         credentials.redact(request_id), http_status)
    data = payload.get("Data")
    translated = data.get("Translated") if isinstance(data, dict) else None
    if not isinstance(translated, str) or not translated.strip():
        raise ProbeError("EmptyTranslation", "接口没有返回有效译文。", request_id, http_status)
    return {"ok": True, "code": 200, "http_status": http_status,
            "request_id": credentials.redact(request_id), "translated": translated,
            "detected_language": data.get("DetectedLanguage"),
            "word_count": data.get("WordCount"), "elapsed_ms": elapsed_ms}


def translate(text, source, target, credentials, timeout=20):
    req = build_request(text, source, target, credentials)
    opener = request.build_opener(request.HTTPSHandler(context=ssl.create_default_context()), NoRedirect())
    started = time.monotonic()
    try:
        try:
            response = opener.open(req, timeout=timeout)
        except error.HTTPError as response_error:
            response = response_error
        with response:
            status = response.code
            payload = read_response(response)
        return parse_result(payload, status, round((time.monotonic() - started) * 1000), credentials)
    except (error.URLError, OSError, TimeoutError) as exc:
        detail = credentials.redact(str(getattr(exc, "reason", exc)))[:300]
        raise ProbeError("NetworkError", "网络/TLS 连接失败：" + detail) from None


def write_report(path, result, credentials):
    if path:
        # 报告只包含结果；绝不序列化 Request 或凭据。
        output = credentials.redact(json.dumps(result, ensure_ascii=False, indent=2))
        Path(path).write_text(output + "\n", encoding="utf-8")


def run_gui(report_path, smoke_test=False):
    import tkinter as tk
    from tkinter import ttk

    window = tk.Tk()
    window.title("阿里云通用翻译 · 本机接口验证")
    window.geometry("780x630")
    if smoke_test:
        window.withdraw()
    panel = ttk.Frame(window, padding=20)
    panel.pack(fill="both", expand=True)
    panel.columnconfigure(1, weight=1)
    ttk.Label(panel, text="凭据仅留在本次窗口内存中，不写入代码、报告或输入法设置。", wraplength=710).grid(
        row=0, column=0, columnspan=2, sticky="w", pady=(0, 14))
    fields = {}
    for row, (key, label, env) in enumerate((
        ("id", "AccessKey ID", "ALIBABA_CLOUD_ACCESS_KEY_ID"),
        ("secret", "AccessKey Secret", "ALIBABA_CLOUD_ACCESS_KEY_SECRET"),
        ("token", "STS Token（可选）", "ALIBABA_CLOUD_SECURITY_TOKEN")), 1):
        ttk.Label(panel, text=label).grid(row=row, column=0, sticky="w", padx=(0, 14), pady=5)
        value = tk.StringVar(value=os.environ.get(env, ""))
        ttk.Entry(panel, textvariable=value, show="*").grid(row=row, column=1, sticky="ew", pady=5)
        fields[key] = value
    languages = ttk.Frame(panel)
    languages.grid(row=4, column=0, columnspan=2, sticky="w", pady=10)
    source, target = tk.StringVar(value="auto"), tk.StringVar(value="en")
    for label, value in (("原文语言", source), ("译文语言", target)):
        ttk.Label(languages, text=label).pack(side="left", padx=(0, 8))
        ttk.Entry(languages, textvariable=value, width=10).pack(side="left", padx=(0, 20))
    ttk.Label(panel, text="原文（每点击一次发送一次请求，不自动重试）").grid(row=5, column=0, columnspan=2, sticky="w")
    original = tk.Text(panel, height=4, wrap="word")
    original.grid(row=6, column=0, columnspan=2, sticky="ew", pady=6)
    original.insert("1.0", "你好，欢迎使用珊瑚拼音输入法。")
    status = tk.StringVar(value="等待输入 AccessKey ID / Secret，然后点击「翻译」。")
    ttk.Label(panel, textvariable=status, wraplength=710).grid(row=8, column=0, columnspan=2, sticky="w", pady=8)
    output = tk.Text(panel, height=10, wrap="word", state="disabled")
    output.grid(row=9, column=0, columnspan=2, sticky="nsew")
    panel.rowconfigure(9, weight=1)
    completed = queue.Queue()
    worker = None
    active_credentials = None

    def show_result(result):
        output.configure(state="normal")
        output.delete("1.0", "end")
        output.insert("1.0", json.dumps(result, ensure_ascii=False, indent=2))
        output.configure(state="disabled")

    def start():
        nonlocal worker, active_credentials
        credentials = Credentials(*(fields[key].get().strip() for key in ("id", "secret", "token")))
        text = original.get("1.0", "end-1c")
        src, dst = source.get().strip(), target.get().strip()
        try:
            build_request(text, src, dst, credentials)
        except ProbeError as exc:
            status.set(str(exc))
            return
        active_credentials = credentials
        button.configure(state="disabled")
        status.set("正在通过 HTTPS 调用 TranslateGeneral…")

        def run():
            try:
                result = translate(text, src, dst, credentials)
            except ProbeError as exc:
                result = exc.as_dict()
            except Exception:
                result = {"ok": False, "code": "UnexpectedError", "message": "测试脚本遇到内部错误。"}
            completed.put(result)

        worker = threading.Thread(target=run, name="aliyun-translation-probe")
        worker.start()

    def poll():
        nonlocal worker, active_credentials
        try:
            result = completed.get_nowait()
        except queue.Empty:
            window.after(150, poll)
            return
        worker.join()
        worker = None
        redacted = json.loads(active_credentials.redact(json.dumps(result, ensure_ascii=False)))
        show_result(redacted)
        try:
            write_report(report_path, redacted, active_credentials)
        except OSError:
            status.set("请求已完成，但结果报告保存失败。")
        else:
            status.set("翻译成功。可修改原文或语言继续验证。" if result.get("ok") else "调用未成功，请查看下方错误码。")
        active_credentials = None
        button.configure(state="normal")
        window.after(150, poll)

    def close():
        if worker and worker.is_alive():
            status.set("请求仍在进行，请等待结果后关闭窗口。")
            return
        for value in fields.values():
            value.set("")
        window.destroy()

    button = ttk.Button(panel, text="翻译", command=start)
    button.grid(row=7, column=0, columnspan=2, sticky="e", pady=5)
    window.protocol("WM_DELETE_WINDOW", close)
    window.after(150, poll)
    if smoke_test:
        window.after(200, close)
    window.mainloop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gui", action="store_true", help="本机窗口输入密钥和原文")
    parser.add_argument("--gui-smoke-test", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--text", help="原文；不传则在终端输入")
    parser.add_argument("--source", default="auto")
    parser.add_argument("--target", default="en")
    parser.add_argument("--report", help="可选结果报告路径，不含凭据")
    args = parser.parse_args()
    if args.gui or args.gui_smoke_test:
        run_gui(args.report, args.gui_smoke_test)
        return 0
    credentials = Credentials(
        os.environ.get("ALIBABA_CLOUD_ACCESS_KEY_ID") or getpass.getpass("AccessKey ID: "),
        os.environ.get("ALIBABA_CLOUD_ACCESS_KEY_SECRET") or getpass.getpass("AccessKey Secret: "),
        os.environ.get("ALIBABA_CLOUD_SECURITY_TOKEN", ""))
    try:
        text = args.text if args.text is not None else input("原文: ")
        result = translate(text, args.source, args.target, credentials)
    except ProbeError as exc:
        result = exc.as_dict()
    print(credentials.redact(json.dumps(result, ensure_ascii=False, indent=2)))
    write_report(args.report, result, credentials)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
