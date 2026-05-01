from pathlib import Path


ROOT = Path(__file__).parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_wake_word_interface_exposes_wake_rms_dbfs():
    source = read("main/audio/wake_word.h")

    assert "GetLastWakeRmsDbfs" in source


def test_wake_arbiter_payload_sends_wake_rms_dbfs_without_audio():
    header = read("main/wake_arbiter_client.h")
    source = read("main/wake_arbiter_client.cc")

    assert "RequestSession(const std::string& wake_word, float wake_rms_dbfs)" in header
    assert '"wake_rms_dbfs"' in source
    assert '"wake_audio"' not in source
    assert '"audio"' not in source


def test_application_rejects_invalid_wake_rms_before_gateway_request():
    source = read("main/application.cc")

    assert "GetLastWakeRmsDbfs" in source
    assert "std::isfinite" in source
    assert "wake_rms_dbfs" in source


def test_esp_wake_word_uses_rolling_rms_not_pcm_ring():
    source = read("main/audio/wake_words/esp_wake_word.cc")

    assert "sum_squares" in source
    assert "sample_count" in source
    assert "wake_word_pcm_" not in read("main/audio/wake_words/esp_wake_word.h")
