# ⚡ AutoClicker — C++ WinAPI, без сторонних библиотек

## Возможности
- Выбор кнопки мыши: Left / Right / Middle
- Настройка CPS (clicks per second): 1–100 через слайдер или поле ввода
- Счётчик кликов в реальном времени
- Горячая клавиша **F6** — включить/выключить (работает глобально, даже из другого окна)
- Тёмная тема, custom-рисованные кнопки (WinAPI OwnerDraw)
- Нет сторонних библиотек — только Win32 API и стандартная библиотека C++

---

## Компиляция

### MinGW / g++ (рекомендуется)
```bash
g++ AutoClicker.cpp -o AutoClicker.exe -lgdi32 -luser32 -lcomctl32 -mwindows -std=c++17
```
> Флаг `-mwindows` убирает консольное окно.

### MSVC (Visual Studio / Developer Command Prompt)
```bat
cl AutoClicker.cpp /std:c++17 /EHsc /link user32.lib gdi32.lib comctl32.lib /SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup
```

### CMake (опционально)
```cmake
cmake_minimum_required(VERSION 3.15)
project(AutoClicker)
set(CMAKE_CXX_STANDARD 17)
add_executable(AutoClicker WIN32 AutoClicker.cpp)
target_link_libraries(AutoClicker user32 gdi32 comctl32)
```

---

## Использование
1. Запустите `AutoClicker.exe`
2. Выберите кнопку мыши (LEFT / RIGHT / MIDDLE)
3. Установите CPS ползунком или вводом числа (1–100)
4. Нажмите **СТАРТ** или **F6**
5. Переключитесь в нужное окно — кликер продолжает работать
6. Остановить: **СТОП** или снова **F6**

---

## Требования
- Windows 7 / 8 / 10 / 11 (x86 или x64)
- MinGW-w64 **или** MSVC 2019+
- Запуск от обычного пользователя (права администратора не нужны)
