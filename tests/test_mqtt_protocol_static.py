from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_mqtt_disconnect_closes_audio_channel_without_goodbye_pingpong():
    source = read("main/protocols/mqtt_protocol.cc")
    disconnected = source[
        source.index("mqtt_->OnDisconnected") :
        source.index("mqtt_->OnConnected")
    ]

    assert "CloseAudioChannel(false)" in disconnected
    assert "Server initiated goodbye" not in disconnected


def test_mqtt_close_audio_channel_is_idempotent():
    source = read("main/protocols/mqtt_protocol.cc")
    close_audio = source[
        source.index("void MqttProtocol::CloseAudioChannel") :
        source.index("bool MqttProtocol::OpenAudioChannel")
    ]

    assert "bool was_open = false" in close_audio
    assert "was_open = udp_ != nullptr" in close_audio
    assert "if (!was_open)" in close_audio
    assert close_audio.index("if (!was_open)") < close_audio.index("if (on_audio_channel_closed_ != nullptr)")
