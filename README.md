# OmochaEngine
the entryjs cpp porting
<br>
[entryjs](https://github.com/entrylabs/entryjs)
를 CPP 로 옮긴 엔진 입니다.
<br>
entry hw,카메라 와 마이크(stt 처리), ai 기능들은 전부 제외되어있습니다
**게임엔진** 에 가깝습니다.

### 왜 만들었나요?
Scratch 를 개선한 turbo warp 나
<br>
Scratch 를 C++ 로 옮긴 프로젝트 를보고
<br>
엔트리는 이런거 없을까? 하며 탄생한 프로젝트 입니다.

### 그래서 이거쓰면 뭐가좋아요?
javascript 기반 엔진보다 속도가 빠릅니다
<br>
**.exe 로 배포가 가능합니다.**
<br>
추후 안드로이드 대응예정
<br>
인터넷 이 없어도 실행가능 합니다 (당연)

# 엔진 특수설정
`project.json` 최상단에 아래와 같은 항목을 넣어 사용합니다.
```json
{
  "specialConfig": {
    "brandName": "<브랜드네임>",
    "showFPS":false,
    "showProjectNameUI": true,
    "showZoomSliderUI": false,
    "setZoomfactor": 1.0
  },
 //... 그외 기타 요소들
}
```
### 설명
brandName  브랜드네임 여기에 자기가 적고싶은 문구를 적습니다.
<br>
showProjectNameUI 프로젝트 제목을 출력합니다.
<br>
showZoomSliderUI 뷰포트를 확대/축소 하는 슬라이더 를 표시합니다.
<br>
showFPS fps 를 표시합니다
<br>
setZoomfactor 확대할 배율 설정합니다. (소수점 으로 조절합니다. 최대 3배율)

### 런타임 도움말
실행 파일 이 있는곳 에서 `OmochaEngine.exe -h` 를 입력하시면 사용 가능한 옵션을 볼수 있습니다.
<br>
이 명령어를 입력하면 메모장이 열리며 아래의 내용이 나옵니다.
```
OmochaEngine v1.0.0 by Maiteil
프로젝트 페이지: https://github.com/maiteil/OmochaEngine

사용법: OmochaEngine.exe [옵션]

옵션:
  --setfps <값>      초당 프레임(FPS)을 설정합니다.
                       (기본값: 엔진 내부 설정, 예: 60)
  --useVk <0|1>      Vulkan 렌더러 사용 여부를 설정합니다.
                       0: 사용 안 함 (기본값), 1: 사용 시도
  --setVsync <0|1>   수직 동기화를 설정합니다.
                       0: 비활성, 1: 활성 (기본값)
  -h, --help         이 도움말을 표시하고 종료합니다.

예제:
  OmochaEngine.exe --setfps 120 --setVsync 0
```

# 사용 라이브러리
| Name       | URL                                     |
|------------|-----------------------------------------|
| Rapid Json | https://rapidjson.org/                  |
| SDL        | https://github.com/libsdl-org/SDL       |
| SDL_image  | https://github.com/libsdl-org/SDL_image |
| SDL_ttf    | https://github.com/libsdl-org/SDL_ttf   |
| miniAudio  | http://miniaud.io/                      |

## 계산 블록 구현 현황

### 완료된 기능
- [x] **`calc_basic`**: 10 + 10 (사칙연산: 덧셈, 뺄셈, 곱셈, 나눗셈)
- [x] **`calc_rand`**: 0 부터 10 사이의 무작위 수
- [x] **`coordinate_mouse`**: 마우스 포인터의 X 좌표 값 / Y 좌표 값
- [x] **`coordinate_object`**: (오브젝트)의 X좌표/Y좌표/회전각/이동방향/크기/모양번호/모양이름
- [x] **`quotient_and_mod`**: 10 을(를) 3 (으)로 나눈 몫 / 나머지
- [x] **`get_project_timer_value`**: 타이머 값
- [x] **`choose_project_timer_action`**: 타이머 시작하기 / 멈추기 / 초기화
- [x] **`set_visible_project_timer`**: 타이머 보이기 / 숨기기
- [x] **`calc_operation`**: 10 의 (제곱/제곱근/sin/cos 등)
- [x] **`get_date`**: 현재 년도/월/일/시/분/초 값
- [x] **`distance_something`**: (마우스 포인터) 까지의 거리
- [x] **`length_of_string`**: (엔트리)의 길이
- [x] **`reverse_of_string`**: (엔트리)을(를) 거꾸로 뒤집은 값
- [x] **`combine_something`**: (안녕) 와(과) (엔트리) 합치기 (문자열)
- [x] **`char_at`**: (안녕하세요)의 (1)번째 글자
- [x] **`substring`**: (안녕하세요)의 (2)번째 글자부터 (4)번째 글자까지
- [x] **`count_match_string`**: (엔트리봇은 엔트리 작품을 좋아해)에서 (엔트리)가 포함된 개수
- [x] **`index_of_string`**: (안녕하세요)에서 (하세)의 위치
- [x] **`replace_string`**: (안녕하세요)의 (안녕)을(를) (Hi)로 바꾸기
- [x] **`change_string_case`**: (Hello Entry!)을(를) (대문자/소문자)로 바꾸기
- [x] **`get_block_count`**: (자신)의 블록 수 (오브젝트/장면/전체 블록 수 계산)
- [x] **`change_rgb_to_hex`**: R (255) G (0) B (0) 값을 Hex 코드로 바꾸기
- [x] **`change_hex_to_rgb`**: (#ff0000) 코드의 (R) 값
- [x] **`get_boolean_value`**: (판단 블록) 값 (결과를 "TRUE" 또는 "FALSE" 문자열로 반환)

### 구현불가
###### 개발자 가 서버를 구축안해서 못씁니다.
- [ ] **`get_user_name`**: 사용자 아이디 (이것은 네이버 서버 전용 기능입니다)
- [ ] **`get_nickname`**: 사용자 닉네임 (이것은 네이버 서버 전용 기능입니다)

## 움직이기 블록 구현 현황
### 완료된 기능
-   [x] **`move_direction`**: (숫자) 만큼 (방향)으로 이동하기
-   [x] **`bounce_wall`**: 화면 끝에 닿으면 튕기기
-   [x] **`move_x`**: x좌표를 (숫자) 만큼 바꾸기
-   [x] **`move_y`**: y좌표를 (숫자) 만큼 바꾸기
-   [x] **`move_xy_time`**: (시간)초 동안 x, y 만큼 움직이기
-   [x] **`locate_x`**: x좌표를 (숫자)(으)로 정하기
-   [x] **`locate_y`**: y좌표를 (숫자)(으)로 정하기
### 미구현
-   [ ] **`locate_xy`**: x: (숫자) y: (숫자) 위치로 이동하기
-   [ ] **`locate_xy_time`**: (시간)초 동안 x: (숫자) y: (숫자) 위치로 이동하기
-   [ ] **`locate`**: (오브젝트 또는 마우스 포인터) 위치로 이동하기
-   [ ] **`locate_object_time`**: (시간)초 동안 (오브젝트 또는 마우스 포인터) 위치로 이동하기
-   [ ] **`rotate_relative`**: (각도) 만큼 회전하기
-   [ ] **`direction_relative`**: 이동 방향을 (각도) 만큼 회전하기
-   [ ] **`rotate_by_time`**: (시간)초 동안 (각도) 만큼 회전하기
-   [ ] **`direction_relative_duration`**: (시간)초 동안 이동 방향을 (각도) 만큼 회전하기
-   [ ] **`rotate_absolute`**: 회전 각도를 (각도)(으)로 정하기
-   [ ] **`direction_absolute`**: 이동 방향을 (각도)(으)로 정하기
-   [ ] **`see_angle_object`**: (오브젝트 또는 마우스 포인터) 쪽 바라보기
-   [ ] **`move_to_angle`**: (각도) 방향으로 (숫자) 만큼 이동하기

### 구현 불가 (또는 현재 미지원)

