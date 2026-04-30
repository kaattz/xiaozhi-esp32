from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_announcement_client_exists_and_uses_gateway_pagination():
    header = read("main/announcement_audio_client.h")
    source = read("main/announcement_audio_client.cc")

    assert "class AnnouncementAudioClient" in header
    assert "AnnouncementAudioFrames" in header
    assert "AnnouncementMode" in header
    assert "listen_after_playback" in header
    assert "listen_timeout_seconds" in header
    assert "FetchFrames(const std::string& text, AnnouncementMode mode" in header
    assert "/announcement/jobs" in source
    assert "frames_base64" in source
    assert "listen_after_playback" in source
    assert "listen_timeout_seconds" in source
    assert 'cJSON_AddStringToObject(payload, "mode"' in source
    assert "kAnnouncementFramesPageLimit" in source
    assert "?offset=" in source
    assert "mbedtls_base64_decode" in source
    assert "body=%s" not in source
    assert "Announcement job response is invalid: %s" not in source


def test_ha_mqtt_exposes_separate_announcement_and_question_text_entities():
    header = read("main/home_assistant_manager.h")
    source = read("main/home_assistant_manager.cc")

    assert "announcement_" in header
    assert "question_" in header
    assert 'new HaEntityText(*ha_bridge_, "Announcement", "announcement"' in source
    assert 'new HaEntityText(*ha_bridge_, "Question", "question"' in source
    assert "PlayRemoteAnnouncement(text, kAnnouncementModeAnnouncement)" in source
    assert "PlayRemoteAnnouncement(text, kAnnouncementModeQuestion)" in source
    assert "InvokeRemoteText(text)" not in source


def test_application_announcement_modes_keep_normal_announcement_private():
    header = read("main/application.h")
    source = read("main/application.cc")

    assert "enum AnnouncementMode" in header
    assert "kAnnouncementModeAnnouncement" in header
    assert "kAnnouncementModeQuestion" in header
    assert "PlayRemoteAnnouncement(const std::string& text, AnnouncementMode mode)" in header
    assert "announcement_listen_token_" in header
    assert "AnnouncementAudioClient" in source
    assert "PushPacketToDecodeQueue" in source
    assert "StartAnnouncementListenTimeout" in source

    body = source.split("void Application::PlayRemoteAnnouncement", 1)[1]
    body = body.split("\nbool Application::CanEnterSleepMode", 1)[0]

    assert "protocol_->SendAudio" not in body
    assert "protocol_->SendStartListening" not in body
    assert "SendStopListening" not in body
    assert "mode == kAnnouncementModeQuestion" in body
    assert "OpenAudioChannel" in body
    assert "SetListeningMode(kListeningModeAutoStop)" in body
    assert "audio_service_.PushPacketToDecodeQueue" in body
    question_branch = body.split("if (mode == kAnnouncementModeQuestion && audio.listen_after_playback)", 1)[1]
    question_branch = question_branch.split("SetListeningMode(kListeningModeAutoStop)", 1)[0]
    assert "audio.listen_timeout_seconds <= 0" in question_branch
    assert "protocol_->CloseAudioChannel()" in question_branch
    assert "SetDeviceState(kDeviceStateIdle)" in question_branch
    assert "AbortSpeaking(kAbortReasonNone);\n            ESP_LOGW(TAG, \"Ignore announcement while device is speaking\")" not in body
    assert "++announcement_listen_token_" in body
    assert "listen_token" in source
    assert "announcement_listen_token_ != listen_token" in source

    timeout_failure_branch = source.split("if (task_created != pdPASS)", 1)[1]
    timeout_failure_branch = timeout_failure_branch.split("bool Application::CanEnterSleepMode", 1)[0]
    assert "++announcement_listen_token_" in timeout_failure_branch
    assert "protocol_->CloseAudioChannel()" in timeout_failure_branch
    assert "SetDeviceState(kDeviceStateIdle)" in timeout_failure_branch
