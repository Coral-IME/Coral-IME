#include "tencent_protocol.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace coral::speech;
namespace {
void require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
AudioClip pcm(std::size_t size = 2) {
    AudioClip result;
    result.format = AudioFormat::kPcm;
    result.bytes.resize(size);
    return result;
}
AudioClip wav() {
    AudioClip result;
    result.bytes = {'R','I','F','F', 38,0,0,0, 'W','A','V','E',
        'f','m','t',' ',16,0,0,0, 1,0, 1,0, 0x80,0x3e,0,0,
        0,0x7d,0,0, 2,0,16,0, 'd','a','t','a',2,0,0,0, 0,0};
    return result;
}
HttpResponse success() {
    return {Failure::kNone, 200, R"({"Response":{"Result":"腾讯云语音识别欢迎您。","AudioDuration":2430,"RequestId":"fixture"}})"};
}
void protocol() {
    require(detail::hex(detail::sha256("abc")) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 vector");
    require(detail::hex(detail::hmac_sha256(std::string(20, '\x0b'), "Hi There")) ==
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", "RFC4231 vector");
    require(detail::hex(detail::hmac_sha256(std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First")) ==
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", "RFC4231 long key");
    const auto request = detail::make_request(pcm(), {"id", "key", "temporary-token"}, {}, 1551113065);
    require(request.url == "https://asr.tencentcloudapi.com/", "endpoint");
    require(request.body == R"({"EngSerViceType":"16k_zh","SourceType":1,"VoiceFormat":"pcm","Data":"AAA=","DataLen":2})", "upload payload fixture");
    bool found_signature = false, found_token = false;
    for (const auto& header : request.headers) {
        if (header.first == "Authorization") {
            require(header.second == "TC3-HMAC-SHA256 Credential=id/2019-02-25/asr/tc3_request, SignedHeaders=content-type;host, Signature=074dc64ea19c6d06fe46886b9944976d91f4ffe65ecdca3c31a481c53bfe549f", "Python hashlib independent TC3 fixture, UTC date");
            found_signature = true;
        }
        if (header.first == "X-TC-Token") found_token = header.second == "temporary-token";
    }
    require(found_signature && found_token, "signature and STS token");
    require(detail::make_request(pcm(4), {"id", "key"}, {}, 1551113065).body.find("AAAAAA==") != std::string::npos, "base64 padding");
    require(detail::make_request(pcm(6), {"id", "key"}, {}, 1551113065).body.find("AAAAAAAA") != std::string::npos, "base64 full group");
    const auto parsed = detail::parse_response(success());
    require(parsed.ok() && parsed.text == "腾讯云语音识别欢迎您。" && parsed.request_id == "fixture" && parsed.audio_duration_ms == 2430, "complete UTF8 text");
    auto reply = success();
    reply.body = R"({"Response":{"Result":"","AudioDuration":0,"RequestId":"silent"}})";
    require(detail::parse_response(reply).ok(), "silent audio");
    reply.body = R"({"Response":{"Error":{"Code":"AuthFailure.SignatureFailure","Message":"secret must not escape"},"RequestId":"auth"}})";
    const auto error = detail::parse_response(reply);
    require(error.failure == Failure::kCredentials && error.provider_code == "AuthFailure.SignatureFailure" && error.text.empty() && error.request_id == "auth", "authentication mapping");
    reply.body = R"({"Response":{"Error":{"Code":"FailedOperation.UserNotRegistered"},"RequestId":"registration"}})";
    require(detail::parse_response(reply).failure == Failure::kRejected, "cloud business error");
    for (const auto* body : {"{}", "[]", "broken", R"({"Response":{"Result":10,"AudioDuration":1,"RequestId":"id"}})",
        R"({"Response":{"Result":"x","AudioDuration":-1,"RequestId":"id"}})",
        R"({"Response":{"Result":"x","AudioDuration":60001,"RequestId":"id"}})",
        R"({"Response":{"Result":"x","Result":"y","AudioDuration":1,"RequestId":"id"}})",
        R"({"Response":{"Result":"x\u0000","AudioDuration":1,"RequestId":"id"}})",
        R"({"Response":{"Result":"x","AudioDuration":1,"RequestId":"id"}})" " trailing"}) {
        reply.body = body;
        require(detail::parse_response(reply).failure == Failure::kInvalidResponse, "malformed response");
    }
    reply = success(); reply.status = 503;
    require(detail::parse_response(reply).failure == Failure::kHttp, "HTTP failure cannot succeed");
    reply.body.assign(kMaxResponseBytes + 1, 'x');
    require(detail::parse_response(reply).failure == Failure::kInvalidResponse, "response limit");
}
void boundaries() {
    int calls = 0;
    TencentRecognizer recognizer({"id", "key"}, {}, [&](const HttpRequest&, const RequestControl&) {
        ++calls; return success();
    });
    require(recognizer.recognize(wav()).ok() && calls == 1, "WAV through provider");
    require(recognizer.recognize(pcm(16000 * 2 * 60)).ok() && calls == 2, "60-second boundary");
    for (auto audio : {pcm(0), pcm(1), pcm(16000 * 2 * 60 + 2), pcm(kMaxAudioBytes + 1)})
        require(recognizer.recognize(audio).failure == Failure::kInvalidAudio, "PCM bounds");
    auto audio = pcm(); audio.sample_rate = 8000;
    require(recognizer.recognize(audio).failure == Failure::kInvalidAudio, "PCM engine rate mismatch");
    for (const std::size_t offset : {4u, 16u, 20u, 22u, 24u, 28u, 32u, 34u, 40u}) {
        auto invalid = wav(); invalid.bytes[offset] ^= 1;
        require(recognizer.recognize(invalid).failure == Failure::kInvalidAudio, "invalid WAV header");
    }
    audio = wav(); audio.bytes.pop_back();
    require(recognizer.recognize(audio).failure == Failure::kInvalidAudio, "truncated WAV");
    require(calls == 2, "invalid audio must not reach transport");
    TencentRecognizer empty({}, {}, [&](const HttpRequest&, const RequestControl&) { ++calls; return success(); });
    require(empty.recognize(pcm()).failure == Failure::kMissingCredentials && calls == 2, "credentials gate");
    TencentRecognizer injection({"id\r\nInjected:x", "key"});
    require(injection.recognize(pcm()).failure == Failure::kMissingCredentials, "header injection gate");
    std::atomic<bool> stopped{true};
    RequestControl control; control.cancelled = &stopped;
    require(recognizer.recognize(pcm(), control).failure == Failure::kCancelled && calls == 2, "pre-cancel");
    control.cancelled = nullptr; control.timeout = std::chrono::milliseconds(0);
    require(recognizer.recognize(pcm(), control).failure == Failure::kTimeout && calls == 2, "zero timeout");
    stopped = false;
    TencentRecognizer cancelling({"id", "key"}, {}, [&](const HttpRequest&, const RequestControl&) {
        stopped = true; return success();
    });
    control = {}; control.cancelled = &stopped;
    require(cancelling.recognize(pcm(), control).failure == Failure::kCancelled, "in-flight cancel discards late success");
    TencentRecognizer throwing({"id", "key"}, {}, [](const HttpRequest&, const RequestControl&) -> HttpResponse { throw std::runtime_error("failure"); });
    require(throwing.recognize(pcm()).failure == Failure::kInternal, "transport exception");
    TencentRecognizer slow({"id", "key"}, {}, [](const HttpRequest&, const RequestControl&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30)); return success();
    });
    control = {}; control.timeout = std::chrono::milliseconds(10);
    require(slow.recognize(pcm(), control).failure == Failure::kTimeout, "late response");
    TencentRecognizer offline({"id", "key"}, {}, [](const HttpRequest&, const RequestControl&) { return HttpResponse{Failure::kNetwork}; });
    require(offline.recognize(pcm()).failure == Failure::kNetwork, "network failure");
    const auto original = wav(); audio = original;
    const auto missing = std::filesystem::temp_directory_path() / ("coral-missing-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(load_audio_file(missing, AudioFormat::kWav, audio) == Failure::kInvalidAudio && audio.bytes == original.bytes, "file failure preserves output");
    auto file_path = missing; file_path += ".wav";
    {
        std::ofstream file(file_path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(original.bytes.data()), original.bytes.size());
    }
    const auto loaded = load_audio_file(file_path, AudioFormat::kWav, audio);
    std::filesystem::remove(file_path);
    require(loaded == Failure::kNone && audio.bytes == original.bytes, "local file upload");
    // 真实 curl 路径拒绝非 HTTPS，避免仅覆盖替身传输。
    HttpRequest http; http.url = "http://127.0.0.1:1/";
    require(detail::send_https(http, {}).failure == Failure::kNetwork, "HTTPS-only curl transport");
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "file") {
            AudioClip audio;
            require(load_audio_file(std::filesystem::u8path(argv[2]), AudioFormat::kWav, audio) == Failure::kNone &&
                detail::valid_audio(audio, {}), "official WAV file validation");
        }
        else if (argc == 2 && std::string(argv[1]) == "protocol") protocol();
        else boundaries();
        std::cout << "speech tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
