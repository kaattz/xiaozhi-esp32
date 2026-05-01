from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_announcement_client_exists_and_uses_gateway_pagination():
    header = read("main/announcement_audio_client.h")
    source = read("main/announcement_audio_client.cc")

    assert "class AnnouncementAudioClient" in header
    assert "AnnouncementAudioFrames" in header
    assert "AnnouncementMode" not in header
    assert "listen_after_playback" not in header
    assert "listen_timeout_seconds" not in header
    assert "FetchFrames(const std::string& text, AnnouncementAudioFrames& out)" in header
    assert "/announcement/jobs" in source
    assert "frames_base64" in source
    assert "listen_after_playback" not in source
    assert "listen_timeout_seconds" not in source
    assert 'cJSON_AddStringToObject(payload, "mode"' not in source
    assert "kAnnouncementFramesPageLimit" in source
    assert "?offset=" in source
    assert "mbedtls_base64_decode" in source
    assert "body=%s" not in source
    assert "Announcement job response is invalid: %s" not in source


def test_ha_mqtt_exposes_only_announcement_text_entity():
    header = read("main/home_assistant_manager.h")
    source = read("main/home_assistant_manager.cc")

    assert "announcement_" in header
    assert "question_" not in header
    assert 'new HaEntityText(*ha_bridge_, "Announcement", "announcement"' in source
    assert 'new HaEntityText(*ha_bridge_, "Question", "question"' not in source
    assert "PlayRemoteAnnouncement(text)" in source
    assert "kAnnouncementModeQuestion" not in source
    assert "InvokeRemoteText(text)" not in source


def test_application_announcement_is_playback_only():
    header = read("main/application.h")
    source = read("main/application.cc")

    assert "enum AnnouncementMode" not in header
    assert "kAnnouncementModeAnnouncement" not in header
    assert "kAnnouncementModeQuestion" not in header
    assert "PlayRemoteAnnouncement(const std::string& text)" in header
    assert "announcement_listen_token_" not in header
    assert "AnnouncementAudioClient" in source
    assert "PushPacketToDecodeQueue" in source
    assert "StartAnnouncementListenTimeout" not in source

    body = source.split("void Application::PlayRemoteAnnouncement", 1)[1]
    body = body.split("\nbool Application::CanEnterSleepMode", 1)[0]

    assert "protocol_->SendAudio" not in body
    assert "protocol_->SendStartListening" not in body
    assert "SendStopListening" not in body
    assert "mode == kAnnouncementModeQuestion" not in body
    assert "OpenAudioChannel" not in body
    assert "SetListeningMode(kListeningModeAutoStop)" not in body
    assert "audio_service_.PushPacketToDecodeQueue" in body
    assert "AbortSpeaking(kAbortReasonNone);\n            ESP_LOGW(TAG, \"Ignore announcement while device is speaking\")" not in body
    assert "listen_token" not in source
