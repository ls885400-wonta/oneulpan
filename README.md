# 오늘판

ESP32-S3-Touch-LCD-7B (1024×600) 를 사무실 알림판으로 만드는 프로젝트입니다.
화면에 시계·날짜, 날씨, 구글 캘린더 일정, 급식, 시간표를 보여 줍니다.

## 폴더

| 폴더 | 내용 |
|---|---|
| `firmware/` | 보드 프로그램 (ESP-IDF 5.5 + LVGL 8) |
| `firmware/main/fonts/` | 한글 글꼴 (`tools/make_fonts.py` 로 만듦, 나눔고딕 OFL) |
| `firmware/components/` | 화면·터치 드라이버 (Waveshare 예제, Apache-2.0) |
| `web/` | 설치 웹페이지 (프로그램 설치 + 설정 보내기) |
| `tools/` | 글꼴 변환 도구 |

## 자료를 받아 오는 곳

- 날씨: Open-Meteo (키 필요 없음), 30분마다
- 급식·시간표: 나이스 교육정보 개방 포털, 6시간마다 + 자정·오후 3시 (오후 3시부터는 다음 평일)
- 일정: 구글 캘린더 비공개 iCal 주소, 15분마다 (오늘부터 7일)
- 화면을 누르면 바로 새로 받습니다.

## 만들기 (빌드)

ESP-IDF 5.5 터미널에서:

```
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py merge-bin -o oneulpan.bin
copy build\oneulpan.bin ..\web\firmware\oneulpan.bin
```

글꼴을 바꾸려면 `python tools/make_fonts.py` 를 다시 실행합니다.

## 보드와 주고받는 명령 (USB, 115200)

- `INFO` → `INFO {"ver":..,"ip":..,"cfg":{..}}`
- `CFG {json}` → `CFG OK` 후 다시 시작. 보낸 항목만 바뀝니다.
