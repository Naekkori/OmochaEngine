# OmochaEngine
the entryjs cpp porting
<br>
[entryjs](https://github.com/entrylabs/entryjs)
를 CPP 로 옮긴 엔진 입니다.
<br>
entry hw,카메라 와 마이크(stt 처리), ai 기능들은 전부 제외되어있습니다
**게임엔진** 에 가깝습니다.

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
    "specialConfig":
                {
                    "brandName":"DBDO",
                    "showProjectNameUI":true,
                    "showZoomSlider":false,
                },
                //... 그외 기타 요소들
}
```
설명
<br>
brandName  브랜드네임 여기에 자기가 적고싶은 문구를 적습니다.
<br>
showProjectNameUI 프로젝트 제목을 출력합니다.
<br>
showZoomSlider 뷰포트를 확대/축소 하는 슬라이더 를 표시합니다.