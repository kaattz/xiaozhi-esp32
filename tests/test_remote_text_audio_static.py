from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_zhi_ling_remote_text_entity_is_not_registered():
    ha_source = read("main/home_assistant_manager.cc")
    ha_header = read("main/home_assistant_manager.h")
    app_header = read("main/application.h")
    app_source = read("main/application.cc")
    cmake = read("main/CMakeLists.txt")

    assert "wake_word_invoke_" not in ha_header
    assert '"指令"' not in ha_source
    assert '"wake_word"' not in ha_source
    assert "WakeWordInvoke(text)" not in ha_source
    assert "InvokeRemoteText(text)" not in ha_source
    assert "InvokeRemoteText" not in app_header
    assert "InvokeRemoteText" not in app_source
    assert "remote_text_audio_client.h" not in app_source
    assert "remote_text_audio_client.cc" not in cmake
