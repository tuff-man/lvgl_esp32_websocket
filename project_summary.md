# ESP32 LVGL9 WebSocket 프로젝트 최종 요약

본 문서는 ESP32를 기반으로 한 LVGL 9.0.0 UI 및 웹소켓 데이터 전송 프로젝트의 최종 구현 내용을 정리합니다.

## 1. 프로젝트 사양
*   **하드웨어**: ESP32 (CYD 보드 계통), 320x240 Resolution
*   **드라이버**: TFT_eSPI, XPT2046_Touchscreen
*   **주요 핀**: ADC 34 (IO34), Touch CS 33, IRQ 36
*   **소프트웨어**: Arduino Framework, LVGL 9.0.0, ESPAsyncWebServer (Port 8000)

## 2. 구현 완료 기능

### 2.1 ESP32 UI (LVGL 9)
*   **탭 1 (WiFi)**: 
    *   주변 AP 스캔 및 리스트 표시.
    *   비밀번호 입력을 위한 전용 모달 및 시스템 최상위 레이어 키보드 지원.
    *   연결 상태 및 IP 주소 실시간 출력.
*   **탭 2 (Monitor)**:
    *   `lv_arc` 위젯을 이용한 0~255 범위의 라운드 계기판 구현.
    *   IO34 ADC 값을 매핑하여 수치 및 게이지 실시간 동기화.
    *   웹소켓 클라이언트로부터 수신된 메시지를 출력하는 로그 영역 (1.5배 높이 확장).
*   **참고**:
    *   보드: esp32 by Espressif Systems 3.0.7
    *   라이브러리: ESP Async WebServer by ESP32Async 3.10.0, Async TCP by ESP32Async 3.4.10

### 2.2 웹 클라이언트 (HTML/JS)
*   **실시간 모니터링**: 서버(ESP32)에서 전송받은 ADC 값을 숫자와 텍스트로 표시.
*   **양방향 통신**: ESP32 IP로의 웹소켓 연결/해제 및 텍스트 메시지 송신 기능.
*   **UI 디자인**: 인터랙티브한 다크 테마 적용.

## 3. 주요 트러블슈팅 및 해결 과정
*   **컴파일 에러**: LVGL 9.0.0의 변경된 API(`lv_msgbox_get_active_btn_text` -> 커스텀 이벤트 핸들러, `lv_list_clean` -> `lv_obj_clean`)를 적용하여 해결.
*   **시스템 재부팅 (LWIP Assert)**: 웹소켓 서버 시작 전 `WiFi.mode(WIFI_STA)`를 명시적으로 호출하여 TCP/IP 스택을 정상 초기화함으로 해결.
*   **UI 레이어 문제**: 키보드와 팝업창이 겹치는 문제를 해결하기 위해 팝업창을 상단으로 배치하고 키보드를 시스템 최상위 레이어(`lv_layer_sys`)에 배치.
*   **폰트 가용성**: `Montserrat 12`, `10` 등이 비활성화된 환경에 맞춰 표준인 `Montserrat 14`를 일괄 적용하고 레이아웃을 이에 맞춰 조정.

## 4. 최종 결과물 파일
*   `lvgl_websocket_project.ino`: ESP32 메인 소스 코드 (Refined)
*   `index.html`: 웹 대시보드 클라이언트
*   `lvgl_websocket_plan.md`: 최종 구현 계획서
*   `project_summary.md`: 본 요약 문서

---
*개발자 참고: 모든 코드는 320x240 Landscape 모드에 최적화되어 있습니다.*
