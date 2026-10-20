"""Protocol fixtures; no real credentials or paid translation requests."""

import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "diagnostics"))
import aliyun_translation_probe as probe


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.credentials = probe.Credentials("YourAccessKeyId", "YourAccessKeySecret")

    def test_official_acs3_signature_fixture(self):
        # 固定输入与期望签名来自官方 ACS3 文档。
        headers = probe.sign_headers(
            "POST", "ecs.cn-shanghai.aliyuncs.com", "RunInstances", "2014-05-26",
            b"", self.credentials,
            query="ImageId=win2019_1809_x64_dtc_zh-cn_40G_alibase_20230811.vhd&RegionId=cn-shanghai",
            timestamp="2023-10-26T10:22:32Z", nonce="3156853299f313e23d1673dc12e1703d")
        self.assertTrue(headers["authorization"].endswith(
            "Signature=06563a9e1b43f5dfe96b81484da74bceab24a1d853912eee15083a6f0f3283c0"))

    def test_utf8_form_and_reserved_characters(self):
        text = "你好 + 50% & x=y\n第二行 / 😀"
        req = probe.build_request(text, "auto", "en", self.credentials)
        self.assertEqual(req.full_url, "https://mt.aliyuncs.com/")
        self.assertEqual(req.method, "POST")
        self.assertEqual(parse_qs(req.data.decode())["SourceText"], [text])
        self.assertEqual(parse_qs(req.data.decode())["Scene"], ["general"])
        self.assertIn("content-type;host;x-acs-action", req.get_header("Authorization"))
        self.assertNotIn(self.credentials.access_key_secret.encode(), req.data)

    def test_unique_request_nonce(self):
        first = probe.build_request("你好", "zh", "en", self.credentials)
        second = probe.build_request("你好", "zh", "en", self.credentials)
        self.assertNotEqual(first.get_header("X-acs-signature-nonce"), second.get_header("X-acs-signature-nonce"))

    def test_validation_boundaries(self):
        for text in ("", "  ", "字" * 5001):
            with self.assertRaises(probe.ProbeError):
                probe.build_request(text, "zh", "en", self.credentials)
        probe.build_request("字" * 5000, "zh", "en", self.credentials)
        for source, target in (("zh\nX:bad", "en"), ("zh", "auto"), ("", "en")):
            with self.assertRaises(probe.ProbeError):
                probe.build_request("你好", source, target, self.credentials)
        with self.assertRaises(probe.ProbeError):
            probe.build_request("你好", "zh", "en", probe.Credentials("", ""))

    def test_success_and_preserved_translation(self):
        payload = {"Code": 200, "RequestId": "request-1", "Data": {"Translated": "Hello!\nSecond line", "DetectedLanguage": "zh"}}
        self.assertEqual(probe.parse_result(payload, 200, 7, self.credentials)["translated"], "Hello!\nSecond line")

    def test_http_success_is_not_business_success(self):
        with self.assertRaises(probe.ProbeError) as caught:
            probe.parse_result({"Code": 10010, "Message": "service not enabled"}, 200, 7, self.credentials)
        self.assertEqual(caught.exception.code, "10010")
        with self.assertRaises(probe.ProbeError):
            probe.parse_result({"Code": 200, "Data": {"Translated": "Hello"}}, 500, 7, self.credentials)

    def test_malformed_or_empty_result(self):
        for data in (None, {}, {"Translated": ""}, {"Translated": "  "}, {"Translated": []}):
            with self.assertRaises(probe.ProbeError):
                probe.parse_result({"Code": 200, "Data": data}, 200, 7, self.credentials)
        for raw in (b"not-json", b"[]", b"\xff", b" " * (probe.MAX_RESPONSE_BYTES + 1)):
            with self.assertRaises(probe.ProbeError):
                probe.read_response(io.BytesIO(raw))

    def test_credentials_redacted_from_errors(self):
        with self.assertRaises(probe.ProbeError) as caught:
            probe.parse_result({"Code": "InvalidAccessKey", "Message": "invalid YourAccessKeyId YourAccessKeySecret"}, 403, 7, self.credentials)
        serialized = json.dumps(caught.exception.as_dict())
        self.assertNotIn("YourAccessKeyId", serialized)
        self.assertNotIn("YourAccessKeySecret", serialized)
        self.assertNotIn("YourAccessKeySecret", repr(self.credentials))

    def test_redirect_rejected(self):
        with self.assertRaises(probe.ProbeError):
            probe.NoRedirect().redirect_request(None, None, 302, "Found", {}, "https://example.com/")

    def test_single_network_attempt(self):
        raw = io.BytesIO(b'{"Code":200,"Data":{"Translated":"Hello"}}')
        raw.code = 200
        with patch.object(probe.request, "build_opener") as builder:
            builder.return_value.open.return_value = raw
            result = probe.translate("你好", "zh", "en", self.credentials)
            self.assertTrue(result["ok"])
            builder.return_value.open.assert_called_once()


if __name__ == "__main__":
    unittest.main()
