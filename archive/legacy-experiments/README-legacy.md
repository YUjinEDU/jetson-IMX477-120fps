# IMX477 120fps Jetson 모드

현재 Jetson IMX477 드라이버에 1332x990@120 센서 모드를 추가한 결과물입니다. 기존 3840x2160@30과 1920x1080@60 모드 번호는 유지합니다.

주의: JetPack 7.2의 Argus는 센서 모드와 일치하는 NITO ISP 튜닝 항목도 요구합니다. 기본 `imx477.nito`에는 1332x990 항목이 없으므로, 드라이버와 DT 오버레이만 설치한 상태에서는 120fps 세션이 `knobSetId not found`로 종료됩니다.

추가 모드:

- 센서 출력: 1332x990, 10-bit Bayer, 2-lane CSI, 120fps
- 프로그램 표시 크기: 1280x720
- 물리 센서 모드: `sensor-mode=2`
- `video_test.py`에서 `IMX477_SENSOR_MODE=2`로 선택

현재 stock `imx477.nito`에는 1332x990@120용 ISP knobset이 없습니다. 따라서 모드 목록과 센서 등록은 성공하지만 JetPack 7.2 Argus 컬러 스트림은 `knobSetId not found`로 중단됩니다. 임의로 NITO 바이너리를 수정하면 파일이 손상되므로 사용하지 않습니다. 120fps 컬러 스트림에는 카메라 모듈에 맞는 정식 NITO가 필요합니다.

## 설치

현재 카메라가 정상 동작하는 상태에서 아래 명령을 실행합니다.

```bash
cd ~/codespace
sudo ./imx477-120fps/install.sh
sudo reboot
```

설치 스크립트는 기존 커널 모듈과 DTBO를 `/root/imx477-120fps-backup-날짜시간/`에 먼저 백업합니다.

## 120fps 테스트

재부팅 후:

```bash
cd ~/codespace
IMX477_SENSOR_MODE=2 uv run --no-sync python -u video_test.py
```

현재 Jetson의 `imx477.nito`는 센서 모드 0과 1만 튜닝합니다. 120fps는 기존 번호를 보존하기 위해 mode2에 배치했습니다. 터미널 로그에는 다음 값이 보여야 합니다.

```text
Camera mode = 2
Output Stream W = 1332 H = 990
Frame Rate = 120.000000
```

프로그램 창은 `nvvidconv`를 거쳐 1280x720으로 표시됩니다. OpenCV 화면 출력과 GUI 처리 때문에 프로그램에 표시되는 FPS는 센서의 실제 출력률보다 낮을 수 있습니다.

## 원상복구

설치 스크립트가 출력한 백업 디렉터리를 사용합니다.

```bash
sudo cp -a /root/imx477-120fps-backup-YYYYMMDD-HHMMSS/nv_imx477.ko \
  /lib/modules/$(uname -r)/updates/drivers/media/i2c/nv_imx477.ko
sudo cp -a /root/imx477-120fps-backup-YYYYMMDD-HHMMSS/tegra234-p3767-camera-p3768-imx477-A.dtbo \
  /boot/tegra234-p3767-camera-p3768-imx477-A.dtbo
sudo depmod -a
sudo reboot
```
