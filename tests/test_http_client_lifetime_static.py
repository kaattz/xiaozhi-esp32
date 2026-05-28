from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"function body not found: {signature}")


def test_http_client_destructor_always_closes_tcp_before_member_teardown():
    source = read("local_components/esp-ml307/src/http_client.cc")
    destructor = function_body(source, "HttpClient::~HttpClient()")

    assert "Close();" in destructor
    assert "if (connected_)" not in destructor
    assert destructor.index("Close();") < destructor.index("vEventGroupDelete")


def test_http_client_close_detaches_tcp_callbacks_before_disconnect():
    source = read("local_components/esp-ml307/src/http_client.cc")
    close_body = function_body(source, "void HttpClient::Close()")

    assert "return;" not in close_body[: close_body.index("connected_ = false")]
    assert "tcp_->OnStream(nullptr)" in close_body
    assert "tcp_->OnDisconnected(nullptr)" in close_body
    assert "tcp_->Disconnect()" in close_body
    assert close_body.index("tcp_->OnStream(nullptr)") < close_body.index("tcp_->Disconnect()")
    assert close_body.index("tcp_->OnDisconnected(nullptr)") < close_body.index("tcp_->Disconnect()")


def test_http_client_open_replaces_previous_tcp_via_close():
    source = read("local_components/esp-ml307/src/http_client.cc")
    open_body = function_body(source, "bool HttpClient::Open")

    previous_connection = open_body[
        open_body.index("// 如果之前有连接，先关闭") :
        open_body.index("// 重置所有状态")
    ]

    assert "Close();" in previous_connection
    assert "tcp_->Disconnect()" not in previous_connection


def test_esp_tcp_disconnect_waits_for_receive_task_even_after_passive_disconnect():
    source = read("local_components/esp-ml307/src/esp/esp_tcp.cc")
    disconnect_body = function_body(source, "void EspTcp::Disconnect()")
    do_disconnect_body = function_body(source, "void EspTcp::DoDisconnect")

    assert "DoDisconnect(true)" in disconnect_body
    assert "if (!connected_)" not in disconnect_body
    assert "receive_task_handle_ != nullptr" in do_disconnect_body
    assert "xTaskGetCurrentTaskHandle()" in do_disconnect_body
    assert "xEventGroupWaitBits" in do_disconnect_body
    assert "receive_task_handle_ = nullptr" in do_disconnect_body


def test_esp_ml307_dependency_uses_local_override():
    source = read("main/idf_component.yml")

    assert "78/esp-ml307:" in source
    assert "override_path: ../local_components/esp-ml307" in source
