@echo off
cd /d "C:\Users\congh\Downloads\Compressed\xiaozhi-esp32-2.0.3otto2\xiaozhi-esp32-2.0.3"
call "C:\Espressif\frameworks\esp-idf-v5.5\export.bat"
idf.py -B build_otto fullclean
idf.py -B build_otto build
idf.py -B build_otto flash monitor
