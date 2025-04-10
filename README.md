python patch.py
python3 ~/Downloads/G90Tools/encryption/encryption.py "${XKEY}" encrypt ~/workspace/radio_calc/x6100/stm32/x6100_mcu/patched.bin  X6100_BBFW_V1.1.8_240915003_160_fm_comp.xgf
scp X6100_BBFW_V1.1.8_240915003_160_fm_comp.xgf root@xiegu-x6100:/usr/firmware/
