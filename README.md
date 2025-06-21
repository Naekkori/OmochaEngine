# <img src="https://raw.githubusercontent.com/Naekkori/OmochaEngine/refs/heads/main/app.png" aligh="left" width="50px" height="50px"> OmochaEngine

the entryjs cpp porting (Indev)
<br>
[entryjs](https://github.com/entrylabs/entryjs)
를 CPP 로 옮긴 엔진 입니다.
<br>
entry hw,카메라 와 마이크(stt 처리), ai 기능들은 전부 제외 시켜 경량화(?) 하였습니다.
**게임엔진** 에 가깝습니다.

### 왜 만들었나요?

Scratch 를 개선한 turbo warp 나
<br>
Scratch 를 C++ 로 옮긴 프로젝트 를보고
<br>
엔트리는 이런거 없을까? 하며 탄생한 프로젝트 입니다.

### 그래서 이거쓰면 뭐가좋아요?

**멀티코어 프로세싱 을(를) 지원합니다**
<br>
**.exe 로 배포가 가능합니다.** (추후 안드로이드 대응예정)
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
```

### 설명

brandName  브랜드네임 여기에 자기가 적고싶은 문구를 적습니다.
<br>
showProjectNameUI 프로젝트 제목을 출력합니다. (로딩 스크린 전용)
<br>
showZoomSliderUI 뷰포트를 확대/축소 하는 슬라이더 를 표시합니다.
<br>
showFPS fps 를 표시합니다
<br>
setZoomfactor 확대할 배율 설정합니다. (소수점 으로 조절합니다. 최대 3배율)

### 런타임 도움말

실행 파일 이 있는곳 에서 cmd(명령프롬프트) 로 `OmochaEngine.exe -h` 를 입력하시면 사용 가능한 옵션을 볼수 있습니다.
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

| Name          | License URL                                                                                                                                                  |
|:--------------|:-------------------------------------------------------------------------------------------------------------------------------------------------------------|
| imGui         | [https://github.com/ocornut/imgui/blob/master/LICENSE.txt](https://github.com/ocornut/imgui/blob/master/LICENSE.txt)                                         |
| libarchive    | [https://github.com/libarchive/libarchive/blob/master/COPYING]("https://github.com/libarchive/libarchive/blob/master/COPYING")                               |
| nlomannJson   | [https://github.com/nlohmann/json/blob/develop/LICENSE.MIT](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT)                                       |
| SDL           | [https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt](https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt)                                           |
| SDL_image     | [https://github.com/libsdl-org/SDL_image/blob/main/LICENSE.txt](https://github.com/libsdl-org/SDL_image/blob/main/LICENSE.txt)                               |
| SDL_ttf       | [https://github.com/libsdl-org/SDL_ttf/blob/main/LICENSE.txt](https://github.com/libsdl-org/SDL_ttf/blob/main/LICENSE.txt)                                   |
| miniAudio     | [https://github.com/mackron/miniaudio/blob/master/LICENSE](https://github.com/mackron/miniaudio/blob/master/LICENSE)                                         |
| Bzip2         | [https://sourceware.org/git/?p=bzip2.git;a=blob_plain;f=LICENSE;hb=bzip2-1.0.6](https://sourceware.org/git/?p=bzip2.git;a=blob_plain;f=LICENSE;hb=bzip2-1.0.6)                                                                                 |
| libjpeg-turbo | [https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md)                   |
| libtiff       | [https://gitlab.com/libtiff/libtiff/-/blob/master/COPYRIGHT](https://gitlab.com/libtiff/libtiff/-/blob/master/COPYRIGHT)                                     |
| Brotli        | [https://github.com/google/brotli/blob/master/LICENSE](https://github.com/google/brotli/blob/master/LICENSE)                                                 |
| Freetype      | [https://gitlab.freedesktop.org/freetype/freetype/-/blob/master/docs/FTL.TXT](https://gitlab.freedesktop.org/freetype/freetype/-/blob/master/docs/FTL.TXT)   |
| libpng        | [http://www.libpng.org/pub/png/src/libpng-LICENSE.txt](http://www.libpng.org/pub/png/src/libpng-LICENSE.txt)                                                 |
| libwebp       | [https://chromium.googlesource.com/webm/libwebp/+/refs/heads/main/COPYING](https://chromium.googlesource.com/webm/libwebp/+/refs/heads/main/COPYING)         |
| xz-utils      | [https://tukaani.org/xz/xz-license.txt](https://tukaani.org/xz/xz-license.txt)                                                                               |
| zlib          | [https://zlib.net/zlib_license.html](https://zlib.net/zlib_license.html)                                                                                     |
| liblzma       | [https://raw.githubusercontent.com/kobolabs/liblzma/refs/heads/master/COPYING](https://raw.githubusercontent.com/kobolabs/liblzma/refs/heads/master/COPYING) |

# 라이센스

```
MIT License

Copyright (c) 2025 내꼬리

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## 계산 블록 구현 현황

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

###### 개발자 가 서버를 구축안해서 못씁니다

- [ ] **`get_user_name`**: 사용자 아이디 (이것은 네이버 서버 전용 기능입니다)
- [ ] **`get_nickname`**: 사용자 닉네임 (이것은 네이버 서버 전용 기능입니다)

## 움직이기 블록 구현 현황

- [x] **`move_direction`**: (숫자) 만큼 (방향)으로 이동하기
- [x] **`bounce_wall`**: 화면 끝에 닿으면 튕기기
- [x] **`move_x`**: x좌표를 (숫자) 만큼 바꾸기
- [x] **`move_y`**: y좌표를 (숫자) 만큼 바꾸기
- [x] **`move_xy_time`**: (시간)초 동안 x, y 만큼 움직이기
- [x] **`locate_x`**: x좌표를 (숫자)(으)로 정하기
- [x] **`locate_y`**: y좌표를 (숫자)(으)로 정하기
- [x] **`locate_xy_time`**: (시간)초 동안 x: (숫자) y: (숫자) 위치로 이동하기 (원본 엔트리 에서 move_xy_time 하고 같은 구현으로 확인)
- [x] **`locate_xy`**: x: (숫자) y: (숫자) 위치로 이동하기
- [x] **`locate`**: (오브젝트 또는 마우스 포인터) 위치로 이동하기
- [x] **`locate_object_time`**: (시간)초 동안 (오브젝트 또는 마우스 포인터) 위치로 이동하기
- [x] **`rotate_relative`**: (각도) 만큼 회전하기
- [x] **`direction_relative`**: 이동 방향을 (각도) 만큼 회전하기 (같은코드)
- [x] **`rotate_by_time`**: (시간)초 동안 (각도) 만큼 회전하기
- [X] **`direction_relative_duration`**: (시간)초 동안 이동 방향을 (각도) 만큼 회전하기 (같은코드)
- [x] **`rotate_absolute`**: 회전 각도를 (각도)(으)로 정하기
- [x] **`direction_absolute`**: 이동 방향을 (각도)(으)로 정하기
- [x] **`see_angle_object`**: (오브젝트 또는 마우스 포인터) 쪽 바라보기
- [x] **`move_to_angle`**: (각도) 방향으로 (숫자) 만큼 이동하기

## 모양새 블럭 구현

- [x] `show`: 엔티티를 보이도록 설정합니다.
- [x] `hide`: 엔티티를 숨기도록 설정합니다.
- [x] `dialog_time`: 지정된 시간 동안 말풍선/생각풍선을 표시합니다. (말하기, 생각하기 옵션 포함)
- [x] `dialog`: 말풍선/생각풍선을 표시합니다. (말하기, 생각하기 옵션 포함, 시간제한 없음)
- [x] `remove_dialog`: 표시된 말풍선/생각풍선을 제거합니다.
- [x] `change_to_some_shape`: 엔티티의 모양을 지정된 모양으로 변경합니다. (ID 또는 이름으로 모양 선택)
- [x] `change_to_next_shape`: 엔티티의 모양을 다음 또는 이전 모양으로 변경합니다. (다음, 이전 옵션 포함)
- [x] `add_effect_amount`: 엔티티에 그래픽 효과(색깔, 밝기, 투명도)를 지정된 값만큼 더합니다.
- [x] `change_effect_amount`: 엔티티의 그래픽 효과(색깔, 밝기, 투명도)를 지정된 값으로 설정합니다.
- [x] `erase_all_effects`: 엔티티에 적용된 모든 그래픽 효과를 제거합니다.
- [x] `change_scale_size`: 엔티티의 크기를 지정된 값만큼 변경합니다. (기존 크기에 더함)
- [x] `set_scale_size`: 엔티티의 크기를 지정된 값으로 설정합니다. (절대 크기)
- [x] `stretch_scale_size`: 엔티티의 가로 또는 세로 크기를 지정된 값만큼 변경합니다. (너비, 높이 옵션)
- [x] `reset_scale_size`: 엔티티의 크기를 원래대로 되돌립니다. (가로/세로 비율 포함)
- [x] `flip_x`: 엔티티를 상하로 뒤집습니다. (Y축 기준 반전)
- [x] `flip_y`: 엔티티를 좌우로 뒤집습니다. (X축 기준 반전)
- [x] `change_object_index`: 엔티티의 그리기 순서를 변경합니다. (맨 앞으로 가져오기, 앞으로 가져오기, 뒤로 보내기, 맨 뒤로 보내기 옵션)

## 소리 블록 구현 상태

### 재생 관련

- [x] `sound_something_with_block`: 소리 재생하기 (예: '소리이름' 재생하기)
- [x] `sound_something_second_with_block`: 소리 (N)초 재생하기 (예: '소리이름' (N)초 재생하기)
- [x] `sound_from_to`: 소리 (시작)초부터 (끝)초까지 재생하기
- [x] `sound_something_wait_with_block`: 소리 재생하고 기다리기 (예: '소리이름' 재생하고 기다리기)
- [x] `sound_something_second_wait_with_block`: 소리 (N)초 재생하고 기다리기 (예: '소리이름' (N)초 재생하고 기다리기)
- [x] `sound_from_to_and_wait`: 소리 (시작)초부터 (끝)초까지 재생하고 기다리기

### 효과 및 제어

- [x] `sound_volume_change`: 소리 크기를 (N)만큼 바꾸기
- [x] `sound_volume_set`: 소리 크기를 (N)%로 정하기
- [x] `get_sound_volume`: 소리 크기 값 (블록)
- [x] `get_sound_speed`: 소리 재생 속도 값 (블록)
- [x] `sound_speed_change`: 소리 재생 속도를 (N)만큼 바꾸기
- [x] `sound_speed_set`: 소리 재생 속도를 (N)으로 정하기
- [x] `sound_silent_all`: 모든 소리 끄기 (옵션: 모든 소리, 이 오브젝트의 소리, 다른 오브젝트의 소리)

### 배경음악

- [x] `play_bgm`: 배경음악 재생하기 ('소리이름')
- [x] `stop_bgm`: 배경음악 끄기

### 정보

- [x] `get_sound_duration`: ('소리이름')의 재생 길이 (초) (블록)

## 변수/리스트 블록 구현

## 변수

- [x] **`ask_and_wait` (묻고 기다리기)**: 사용자 입력 요청 및 대기
- [x] **`get_canvas_input_value` (대답)**: 마지막 입력 값 가져오기
- [x] **`set_visible_answer` (대답 보이기/숨기기)**: 대답 UI 토글
- [x] **`get_variable` (변수 값)**: 변수 값 가져오기
- [x] **`change_variable` (변수 값 바꾸기)**: 변수 값 변경 (덧셈/이어붙이기)
- [x] **`set_variable` (변수 값 정하기)**: 변수 값 설정
- [x] **`show_variable` (변수 보이기)**: 변수 UI 표시
- [x] **`hide_variable` (변수 숨기기)**: 변수 UI 숨김

## 리스트

- [x] **`value_of_index_from_list` (리스트 항목 값)**: 특정 인덱스 항목 값 가져오기
- [x] **`add_value_to_list` (리스트에 항목 추가)**: 맨 뒤에 항목 추가
- [x] **`remove_value_from_list` (리스트에서 항목 삭제)**: 특정 인덱스 항목 삭제
- [x] **`insert_value_to_list` (리스트에 항목 삽입)**: 특정 인덱스에 항목 삽입
- [x] **`change_value_list_index` (리스트 항목 값 바꾸기)**: 특정 인덱스 항목 값 변경
- [x] **`length_of_list` (리스트 길이)**: 리스트 항목 수 가져오기
- [x] **`is_included_in_list` (리스트에 항목 포함 여부)**: 값 포함 여부 확인
- [x] **`show_list` (리스트 보이기)**: 리스트 UI 표시
- [x] **`hide_list` (리스트 숨기기)**: 리스트 UI 숨김

## 흐름 블록 구현

- [x] wait_second (~초 기다리기)
- [x] repeat_basic (~번 반복하기)
- [x] repeat_inf (계속 반복하기)
- [x] repeat_while_true (~가 될 때까지/동안 반복하기)
- [x] stop_repeat (반복 중단하기)
- [x] continue_repeat (반복 처음으로 돌아가기)
- [x] _if (만일 ~이라면)
- [x] if_else (만일 ~이라면, 아니면)
- [x] wait_until_true (~가 될 때까지 기다리기)
- [x] stop_object (모든/자신/다른/이 스크립트 멈추기)
- [x] restart_project (처음부터 다시 실행하기)
- [x] when_clone_start (복제되었을 때)
- [x] create_clone (~의 복제본 만들기)
- [x] delete_clone (이 복제본 삭제하기)
- [x] remove_all_clones (모든 복제본 삭제하기)

## 판단 블록 구현

- [x] **`is_clicked`**: 마우스가 클릭되었는지 판단
- [x] **`is_object_clicked`**: 특정 오브젝트가 클릭되었는지 판단
- [x] **`is_press_some_key`**: 특정 키가 눌렸는지 판단
- [x] **`reach_something`**: 특정 대상(벽, 마우스 포인터, 다른 오브젝트)에 닿았는지 판단
- [x] **`is_type`**: 주어진 값의 타입(숫자, 영어, 한글)이 일치하는지 판단
- [x] **`boolean_basic_operator`**: 두 값의 관계(같음, 다름, 큼, 작음, 크거나 같음, 작거나 같음)를 판단
- [x] **`boolean_and_or`**: 두 불리언 값에 대해 AND 또는 OR 연산을 수행
- [x] **`boolean_not`**: 불리언 값에 대해 NOT 연산을 수행
- [x] **`is_boost_mode`**: 현재 부스트 모드(WebGL 사용 여부)인지 판단
- [x] **`is_current_device_type`**: 현재 장치 유형(데스크톱, 태블릿, 스마트폰)이 일치하는지 판단
## 구현불가
###### 미지원 기능
- [ ] **`is_touch_supported`**: 현재 장치가 터치를 지원하는지 판단

## 텍스트 블록 구현

- [x] **`text_read`**: (글상자)의 글 내용
- [x] **`text_write`**: (글상자)에 (내용) 쓰기
- [x] **`text_append`**: (글상자)에 (내용) 이어 쓰기
- [x] **`text_prepend`**: (글상자)에 (내용) 앞에 이어 쓰기
- [ ] **`text_change_effect`**: (글상자)에 (효과) (적용/해제)하기
- [ ] **`text_change_font`**: (글상자)의 글꼴을 (글꼴)로 바꾸기
- [x] **`text_change_font_color`**: (글상자)의 글자 색을 (색)으로 바꾸기
- [x] **`text_change_bg_color`**: (글상자)의 배경색을 (색)으로 바꾸기
- [ ] **`text_flush`**: (글상자)의 글 모두 지우기
