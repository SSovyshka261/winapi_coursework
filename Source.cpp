#include <Lmcons.h>
#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include "json.hpp"
#include <shlobj.h> 
#include <stack>
#include "resource.h"

using json = nlohmann::json;
using namespace std;


HWND hwnd;
HFONT hFont = CreateFont(
    20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
    DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
    VARIABLE_PITCH | FF_DONTCARE,
    L"Arial"
);

struct Pixel {
    int x, y, r, g, b, color_index;
    bool colored = false; 
};

struct Color {
    int index, r, g, b;
};

struct Size {
    int width, height;
};

struct SelectedColor {
    int r, g, b;
};

SelectedColor selectedColor = { 0, 0, 0 };

stack<Pixel> history;
vector<Pixel> pixels;
vector<Color> colors;
Size canvasSize;

vector<Pixel> loadPixels(const string& filename) {
    ifstream file(filename);
    json j;
    file >> j;

    vector<Pixel> pixels;
    for (const auto& item : j) {
        pixels.push_back({
            item["x"],
            item["y"],
            item["r"],
            item["g"],
            item["b"],
            item["color_index"],
            false 
            });
    }
    return pixels;
}

vector<Color> loadColors(const string& filename) {
    ifstream file(filename);
    json j;
    file >> j;

    vector<Color> colors;
    for (const auto& item : j) {
        colors.push_back({
            item["index"],
            item["r"],
            item["g"],
            item["b"]
            });
    }
    return colors;
}

Size loadSize(const string& filename) {
    ifstream file(filename);
    json j;
    file >> j;
    Size size;

    size.width = j["width"];
    size.height = j["height"];

    return size;
}

const int cellSize = 30;

bool IsImageColoredCorrectly() {
    for (const Pixel& p : pixels) {
        if (!p.colored) return false;

        // Найдём эталонный цвет по color_index
        auto it = find_if(colors.begin(), colors.end(),
            [&](const Color& c) { return c.index == p.color_index; });

        if (it == colors.end()) return false;

        if (p.r != it->r || p.g != it->g || p.b != it->b)
            return false;
    }
    return true;
}

string GetUserNameString() {
    wchar_t name[UNLEN + 1];
    DWORD size = UNLEN + 1;
    GetUserNameW(name, &size);

    // Переводим в std::string
    wstring ws(name);
    return string(ws.begin(), ws.end());
}

void UpdateStats() {
    string username = GetUserNameString();

    json stats;
    ifstream in("stats.json");
    if (in.is_open()) {
        in >> stats;
        in.close();
    }

    if (!stats.contains(username)) {
        stats[username] = { {"name", username}, {"images_done", 0} };
    }

    stats[username]["images_done"] = stats[username]["images_done"].get<int>() + 1;

    ofstream out("stats.json");
    out << setw(4) << stats << endl;
}

void RunConverter() {
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    // Относительный путь к программе
    LPCWSTR exePath = L"convertor.exe";

    // Создаем копию строки для CreateProcess, так как она модифицирует командную строку
    wchar_t cmdLine[MAX_PATH];
    wcscpy_s(cmdLine, exePath);

    BOOL result = CreateProcess(
        NULL,       // Имя модуля
        cmdLine,    // Командная строка
        NULL,       
        NULL,       
        FALSE,      // Наследование дескрипторов
        0,          // Флаги создания
        NULL,       
        NULL,       // Текущий каталог
        &si,        // Информация о запуске
        &pi         // Информация о процессе
    );

    if (!result) {
        std::cerr << "CreateProcess failed with error: " << GetLastError() << std::endl;
        MessageBox(NULL, L"Failed to launch convetor.exe", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

bool CopyFileToRoot(const wstring& sourceFolder, const wstring& filename) {
    wstring src = sourceFolder + L"\\" + filename;
    wstring dst = filename; // в корень проекта
    return CopyFileW(src.c_str(), dst.c_str(), FALSE); // перезаписать, если нужно
}

void SelectImageAndLoad() {
    BROWSEINFO bi = { 0 };
    bi.lpszTitle = L"Выберите папку с изображением";
    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);

    if (pidl != NULL) {
        wchar_t selectedPath[MAX_PATH];
        if (SHGetPathFromIDList(pidl, selectedPath)) {
            // Копируем нужные JSON-файлы
            CopyFileToRoot(selectedPath, L"bitmap.json");
            CopyFileToRoot(selectedPath, L"colors.json");
            CopyFileToRoot(selectedPath, L"size.json");

            // Перезагружаем данные
            pixels = loadPixels("bitmap.json");
            colors = loadColors("colors.json");
            canvasSize = loadSize("size.json");

            // Обновляем окно
            InvalidateRect(hwnd, NULL, TRUE);
            UpdateWindow(hwnd);

			if (!history.empty()) {
				// Очищаем историю, если была перезагрузка
				while (!history.empty()) {
					history.pop();
				}
			}
        }
        CoTaskMemFree(pidl);
    }
}

void ShowStatsWindow() {
    ifstream file("stats.json");
    if (!file.is_open()) {
        MessageBox(hwnd, L"Файл ститистики не знайден! Завершіть хочаб одне зображення", L"Помилка", MB_OK | MB_ICONERROR);
        return;
    }

    json stats;
    file >> stats;

    wstring text = L"Статистика користувача:\n\n";
    for (auto it = stats.begin(); it != stats.end(); ++it) {
        string nameStr = it.value()["name"];
        int done = it.value()["images_done"];

        // Конвертация string -> wstring
        wstring name(nameStr.begin(), nameStr.end());

        text += L"👤 " + name + L" — " + to_wstring(done) + L" Зображень\n";
    }

    MessageBox(hwnd, text.c_str(), L"Статистика", MB_OK | MB_ICONINFORMATION);
}

void DrawGrid(HDC hdc) {
    int grid_size = canvasSize.width * canvasSize.height;

    SelectObject(hdc, hFont);

    for (int i = 0; i < grid_size; ++i) {
        int row = i / canvasSize.width;
        int col = i % canvasSize.width;

        int x1 = col * cellSize;
        int y1 = row * cellSize;
        int x2 = (col + 1) * cellSize;
        int y2 = (row + 1) * cellSize;

        if (i < (int)pixels.size()) {
            // Заливка цветом, если пользователь закрасил
            if (pixels[i].colored) {
                HBRUSH hBrush = CreateSolidBrush(RGB(pixels[i].r, pixels[i].g, pixels[i].b));
                RECT rect = { x1, y1, x2, y2 };
                FillRect(hdc, &rect, hBrush);
                DeleteObject(hBrush);
            }
            else {
                // Белый фон, если не закрашено
                HBRUSH hBrush = CreateSolidBrush(RGB(255, 255, 255));
                RECT rect = { x1, y1, x2, y2 };
                FillRect(hdc, &rect, hBrush);
                DeleteObject(hBrush);
            }

            // Рисуем цифру (color_index)
            int colorIndex = pixels[i].color_index;
            wstring wText = to_wstring(colorIndex);
            const wchar_t* text = wText.c_str();

            SIZE textSize;
            GetTextExtentPoint32(hdc, text, wcslen(text), &textSize);

            int centerX = (x1 + x2) / 2;
            int centerY = (y1 + y2) / 2;

            int textX = centerX - textSize.cx / 2;
            int textY = centerY - textSize.cy / 2;

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0));
            TextOut(hdc, textX, textY, text, wcslen(text));
        }
    }
}

void DrawPalette(HDC hdc) {
    SelectObject(hdc, hFont);

    const int colorBoxSize = 30;
    const int padding = 5;
    const int startX = 10;
    const int startY = canvasSize.height * cellSize + 20;

    for (size_t i = 0; i < colors.size(); i++) {
        Color& color = colors[i];

        int x = startX + (colorBoxSize + padding) * i;
        int y = startY;

        HBRUSH hBrush = CreateSolidBrush(RGB(color.r, color.g, color.b));
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);

        Rectangle(hdc, x, y, x + colorBoxSize, y + colorBoxSize);

        SelectObject(hdc, hOldBrush);
        DeleteObject(hBrush);

        // Рисуем индекс цвета под квадратом
        wstring wText = to_wstring(color.index);
        SIZE textSize;
        GetTextExtentPoint32(hdc, wText.c_str(), wcslen(wText.c_str()), &textSize);

        int textX = x + (colorBoxSize - textSize.cx) / 2;
        int textY = y + colorBoxSize + 2;

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));
        TextOut(hdc, textX, textY, wText.c_str(), wcslen(wText.c_str()));
    }
}

// Обработка клика по палитре — меняем selectedColor
void OnPaletteClick(int mouseX, int mouseY) {
    const int colorBoxSize = 30;
    const int padding = 5;
    const int startX = 10;
    const int startY = canvasSize.height * cellSize + 20;

    for (size_t i = 0; i < colors.size(); i++) {
        int x = startX + (colorBoxSize + padding) * i;
        int y = startY;

        RECT rect = { x, y, x + colorBoxSize, y + colorBoxSize };
        if (mouseX >= rect.left && mouseX <= rect.right &&
            mouseY >= rect.top && mouseY <= rect.bottom) {
            selectedColor.r = colors[i].r;
            selectedColor.g = colors[i].g;
            selectedColor.b = colors[i].b;
            break;
        }
    }
}

void ClearGrid() {
    for (auto& pixel : pixels) {
        pixel.colored = false;
        // Можно вернуть r,g,b к исходным цветам из color_index:
        auto it = find_if(colors.begin(), colors.end(),
            [&](const Color& c) { return c.index == pixel.color_index; });
        if (it != colors.end()) {
            pixel.r = it->r;
            pixel.g = it->g;
            pixel.b = it->b;
        }
    }
    // Очистка истории
    while (!history.empty()) history.pop();

    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
}

void OnGridClick(int mouseX, int mouseY) {
    int col = mouseX / cellSize;
    int row = mouseY / cellSize;

    if (col < 0 || col >= canvasSize.width || row < 0 || row >= canvasSize.height)
        return;

    int pixelIndex = row * canvasSize.width + col;
    if (pixelIndex < (int)pixels.size()) {
        history.push(pixels[pixelIndex]);

        pixels[pixelIndex].r = selectedColor.r;
        pixels[pixelIndex].g = selectedColor.g;
        pixels[pixelIndex].b = selectedColor.b;
        pixels[pixelIndex].colored = true;

        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);

        if (IsImageColoredCorrectly()) {
            UpdateStats();
            MessageBox(hwnd, L"Вітаємо! Зображення розмальовано до кінця!", L"Готово", MB_OK | MB_ICONINFORMATION);
            ClearGrid();
        }
    }
}

void UndoLastPixel() {
    if (!history.empty()) {
        Pixel previous = history.top();
        history.pop();

        int index = previous.y * canvasSize.width + previous.x;
        if (index >= 0 && index < (int)pixels.size()) {
            pixels[index] = previous;
        }

        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        int x = LOWORD(lp);
        int y = HIWORD(lp);

        // Проверяем, клик по палитре или по сетке
        int paletteTop = canvasSize.height * cellSize + 20;
        if (y >= paletteTop) {
            OnPaletteClick(x, y);
        }
        else {
            OnGridClick(x, y);
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        DrawGrid(hdc);
        DrawPalette(hdc);

        EndPaint(hwnd, &ps);
        break;
    }
    case WM_COMMAND: {
        switch(LOWORD(wp)) {
            case ID_OPEN_IMAGE:
                SelectImageAndLoad();
                break;
            case ID_OPEN_CONVERTOR:
                RunConverter();
                break;
            case ID_OPEN_STATS:
			    ShowStatsWindow();
			    break;
			case ID_ABOUT:
                MessageBox(hwnd,
                    L"Number Painter — це програма для розфарбовування зображень за номерами.\n\n"
                    L"Як завантажити нове зображення?\n"
                    L"1. У меню виберіть пункт «Завантажити зображення».\n"
                    L"2. Завантажте зображення у форматі 16x16 BMP.\n"
                    L"3. Програма автоматично згенерує сітку з номерами кольорів.\n\n"
                    L"Як вибрати інше зображення, яке вже було завантажено?\n"
                    L"1. У меню виберіть пункт «Відкрити зображення».\n"
                    L"2. Виберіть папку, де зберігаються файли bitmap.json, colors.json та size.json (програма автоматично їх генерує при завантаженні).\n"
                    L"3. Програма завантажить нове зображення та оновить сітку.\n\n"
                    L"Як розфарбовувати зображення?\n"
                    L"1. Розфарбовуйте клітинки сітки, обираючи колір із палітри.\n"
                    L"2. Щоб обрати колір, натисніть на відповідний квадратик у палітрі під сіткою.\n"
                    L"3. Після повного і правильного розфарбування зображення статистика автоматично оновиться.\n"
                    L"4. У меню можна переглянути статистику виконаних розфарбувань.\n\n"
					L"Горячі клавіші:\n"
                    L"- Якщо ви хочете скасувати останнє розфарбування, натисніть Ctrl + Z.\n\n"
					L"Гарного розфарбовування!",
                    L"Про програму",
                    MB_OK | MB_ICONINFORMATION);


				break;
			case ID_CREDITS:
                MessageBox(hwnd, L"Number Painter\n\nСтворено студентом:\nСавченко Богдан Ігорович\nГрупи КІУКІу-24-1", L"Автор", MB_OK | MB_ICONINFORMATION);
                break;
        }
   
        break;
    }
    case WM_HOTKEY:
        if (wp == 1) { // ID горячей клавиши
            UndoLastPixel();
        }
        break;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, 1);
        PostQuitMessage(0);
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;
    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    HMENU hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_MENU1));
	HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"NumberPainter";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = hIcon;

    if (!RegisterClass(&wc)) {
        MessageBox(NULL, L"Failed to register window class.", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    hwnd = CreateWindowEx(
        0,
        L"NumberPainter",
        L"Number Painter",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 800,
        NULL,
        hMenu,
        hInstance,
        NULL
    );
    RegisterHotKey(hwnd, 1, MOD_CONTROL, 'Z');

    if (!hwnd) {
        MessageBox(NULL, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    // Загрузка данных до основного цикла
    pixels = loadPixels("bitmap.json");
    colors = loadColors("colors.json");
    canvasSize = loadSize("size.json");

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetFocus(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
