/*
 * ac's calc — SDL2 calculator (Win11-inspired layout & colors)
 *
 * Windows (MSVC): from this folder run build_calc_msvc.bat (needs deps\SDL2-* and deps\SDL2_ttf-* VC zips extracted).
 *
 * MinGW / MSYS2:
 *   g++ -std=c++17 calc.cpp -o calc -I/mingw64/include/SDL2 -L/mingw64/lib -lmingw32 -lSDL2main -lSDL2 -lSDL2_ttf
 */

#include <SDL.h>
#include <SDL_ttf.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr int WIN_W = 600;
constexpr int WIN_H = 400;

// Win11-ish palette
constexpr SDL_Color kBg{243, 243, 243, 255};
constexpr SDL_Color kDisplayBg{255, 255, 255, 255};
constexpr SDL_Color kDisplayBorder{229, 229, 229, 255};
constexpr SDL_Color kText{32, 32, 32, 255};
constexpr SDL_Color kTextMuted{96, 96, 96, 255};
constexpr SDL_Color kBtnNum{255, 255, 255, 255};
constexpr SDL_Color kBtnNumBorder{218, 218, 218, 255};
constexpr SDL_Color kBtnOp{241, 248, 255, 255};
constexpr SDL_Color kBtnOpBorder{199, 224, 255, 255};
constexpr SDL_Color kBtnEq{0, 103, 192, 255}; // ~ Win11 accent
constexpr SDL_Color kBtnEqText{255, 255, 255, 255};
constexpr SDL_Color kBtnHover{248, 248, 248, 255};
constexpr SDL_Color kBtnHoverOp{236, 244, 252, 255};
constexpr SDL_Color kBtnHoverEq{0, 118, 214, 255};

enum class BtnKind { Digit, Dot, Ce, Clear, Back, Sign, Divide, Multiply, Subtract, Add, Equals };

struct Button {
    SDL_Rect rect{};
    BtnKind kind{};
    std::string label;
    bool opStyle = false;
    bool eqStyle = false;
};

struct CalcState {
    std::string display = "0";
    double accumulator = 0;
    BtnKind pending = BtnKind::Add; // sentinel: no-op until first op uses current
    bool hasPending = false;
    bool freshNumber = true;
    bool error = false;

    void setError() {
        error = true;
        display = "Error";
        hasPending = false;
        freshNumber = true;
    }

    void normalizeDisplay() {
        if (display.empty()) display = "0";
    }

    void inputDigit(char d) {
        if (error) return;
        if (freshNumber) {
            display = d == '0' ? std::string("0") : std::string(1, d);
            freshNumber = false;
        } else {
            if (display == "0" && d != '0')
                display = std::string(1, d);
            else if (display == "0" && d == '0')
                ;
            else {
                if (display.length() < 14) display += d;
            }
        }
    }

    void inputDot() {
        if (error) return;
        if (freshNumber) {
            display = "0.";
            freshNumber = false;
        } else if (display.find('.') == std::string::npos && display.size() < 13) {
            display += '.';
        }
    }

    double displayValue() const { return error ? 0 : std::stod(display); }

    static double apply(double a, BtnKind op, double b) {
        switch (op) {
            case BtnKind::Add: return a + b;
            case BtnKind::Subtract: return a - b;
            case BtnKind::Multiply: return a * b;
            case BtnKind::Divide:
                if (b == 0.0) return std::numeric_limits<double>::quiet_NaN();
                return a / b;
            default: return b;
        }
    }

    static std::string format(double v) {
        if (!std::isfinite(v)) return "Error";
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.10g", v);
        std::string s(buf);
        if (s.size() > 14) std::snprintf(buf, sizeof buf, "%.6e", v), s = buf;
        return s;
    }

    void flushOp(BtnKind newOp) {
        if (error) return;
        double cur = displayValue();
        if (!hasPending) {
            accumulator = cur;
            hasPending = true;
        } else if (!freshNumber) {
            double r = apply(accumulator, pending, cur);
            if (std::isnan(r)) {
                setError();
                return;
            }
            accumulator = r;
            display = format(r);
        }
        pending = newOp;
        freshNumber = true;
    }

    void equals() {
        if (error) return;
        if (!hasPending) return;
        double cur = displayValue();
        double r = apply(accumulator, pending, cur);
        if (std::isnan(r)) {
            setError();
            return;
        }
        display = format(r);
        accumulator = 0;
        hasPending = false;
        freshNumber = true;
    }

    void clearAll() {
        display = "0";
        accumulator = 0;
        pending = BtnKind::Add;
        hasPending = false;
        freshNumber = true;
        error = false;
    }

    void clearEntry() {
        if (error) clearAll();
        else {
            display = "0";
            freshNumber = true;
        }
    }

    void backspace() {
        if (error || freshNumber) return;
        if (!display.empty()) display.pop_back();
        normalizeDisplay();
    }

    void negate() {
        if (error || display.empty() || display == "0") return;
        if (display[0] == '-')
            display.erase(0, 1);
        else
            display.insert(0, 1, '-');
    }
};

SDL_Texture *makeLabel(SDL_Renderer *ren, TTF_Font *font, const std::string &text, SDL_Color fg) {
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text.c_str(), fg);
    if (!surf) return nullptr;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    return tex;
}

void drawTexture(SDL_Renderer *ren, SDL_Texture *tex, const SDL_Rect &box, bool center) {
    if (!tex) return;
    int tw, th;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    SDL_Rect dst;
    dst.w = tw;
    dst.h = th;
    if (center) {
        dst.x = box.x + (box.w - tw) / 2;
        dst.y = box.y + (box.h - th) / 2;
    } else {
        dst.x = box.x + box.w - tw - 16;
        dst.y = box.y + (box.h - th) / 2;
    }
    SDL_RenderCopy(ren, tex, nullptr, &dst);
}

void fillRect(SDL_Renderer *ren, const SDL_Rect &r, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren, &r);
}

void strokeRect(SDL_Renderer *ren, const SDL_Rect &r, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(ren, &r);
}

void drawButtonFace(SDL_Renderer *ren, const Button &b, int mx, int my, bool pressed) {
    bool hover = mx >= b.rect.x && mx < b.rect.x + b.rect.w && my >= b.rect.y && my < b.rect.y + b.rect.h;
    SDL_Color fill = kBtnNum;
    SDL_Color border = kBtnNumBorder;
    if (b.eqStyle) {
        fill = pressed || hover ? kBtnHoverEq : kBtnEq;
        border = kBtnEq;
    } else if (b.opStyle) {
        fill = hover ? kBtnHoverOp : kBtnOp;
        border = kBtnOpBorder;
    } else {
        fill = hover ? kBtnHover : kBtnNum;
    }
    fillRect(ren, b.rect, fill);
    strokeRect(ren, b.rect, border);
}

const char *pickFontPath() {
    static const char *candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/SegoeUIVF.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    };
    for (const char *p : candidates) {
        if (!p) continue;
        FILE *f = std::fopen(p, "rb");
        if (f) {
            std::fclose(f);
            return p;
        }
    }
    return nullptr;
}

std::vector<Button> buildLayout() {
    const int margin = 18;
    const int gap = 8;
    const int dispH = 72;
    const int topY = margin;
    const int gridTop = topY + dispH + gap * 2;
    const int usableW = WIN_W - 2 * margin;
    const int bottomSpace = margin;
    const int gridH = WIN_H - gridTop - bottomSpace;
    const int cols = 4;
    const int rows = 5;
    const int cellW = (usableW - gap * (cols - 1)) / cols;
    const int cellH = (gridH - gap * (rows - 1)) / rows;

    std::vector<Button> v;
    auto add = [&](int c, int r, BtnKind k, const char *lab, bool op = false, bool eq = false) {
        Button b;
        b.rect = {margin + c * (cellW + gap), gridTop + r * (cellH + gap), cellW, cellH};
        b.kind = k;
        b.label = lab;
        b.opStyle = op;
        b.eqStyle = eq;
        v.push_back(b);
    };

    // Row 0 — top row (Win11-style shortcuts)
    add(0, 0, BtnKind::Ce, "CE", true);
    add(1, 0, BtnKind::Clear, "C", true);
    add(2, 0, BtnKind::Back, "⌫", true);
    add(3, 0, BtnKind::Divide, "÷", true);

    add(0, 1, BtnKind::Digit, "7", false);
    add(1, 1, BtnKind::Digit, "8", false);
    add(2, 1, BtnKind::Digit, "9", false);
    add(3, 1, BtnKind::Multiply, "×", true);

    add(0, 2, BtnKind::Digit, "4", false);
    add(1, 2, BtnKind::Digit, "5", false);
    add(2, 2, BtnKind::Digit, "6", false);
    add(3, 2, BtnKind::Subtract, "−", true);

    add(0, 3, BtnKind::Digit, "1", false);
    add(1, 3, BtnKind::Digit, "2", false);
    add(2, 3, BtnKind::Digit, "3", false);
    add(3, 3, BtnKind::Add, "+", true);

    add(0, 4, BtnKind::Sign, "±", true);
    add(1, 4, BtnKind::Digit, "0", false);
    add(2, 4, BtnKind::Dot, ".", false);
    add(3, 4, BtnKind::Equals, "=", true, true);

    return v;
}

SDL_Rect displayRect() {
    const int margin = 18;
    const int dispH = 72;
    return {margin, margin, WIN_W - 2 * margin, dispH};
}

void applyButton(CalcState &st, const Button &b) {
    switch (b.kind) {
        case BtnKind::Digit:
            st.inputDigit(b.label[0]);
            break;
        case BtnKind::Dot:
            st.inputDot();
            break;
        case BtnKind::Ce:
            st.clearEntry();
            break;
        case BtnKind::Clear:
            st.clearAll();
            break;
        case BtnKind::Back:
            st.backspace();
            break;
        case BtnKind::Sign:
            st.negate();
            break;
        case BtnKind::Divide:
            st.flushOp(BtnKind::Divide);
            break;
        case BtnKind::Multiply:
            st.flushOp(BtnKind::Multiply);
            break;
        case BtnKind::Subtract:
            st.flushOp(BtnKind::Subtract);
            break;
        case BtnKind::Add:
            st.flushOp(BtnKind::Add);
            break;
        case BtnKind::Equals:
            st.equals();
            break;
    }
}

const Button *hitTest(const std::vector<Button> &btns, int x, int y) {
    for (const auto &b : btns) {
        if (x >= b.rect.x && x < b.rect.x + b.rect.w && y >= b.rect.y && y < b.rect.y + b.rect.h)
            return &b;
    }
    return nullptr;
}

} // namespace

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() < 0) {
        std::fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    const char *fontPath = pickFontPath();
    if (!fontPath) {
        std::fprintf(stderr, "No suitable font file found. Install Segoe UI or DejaVu Sans.\n");
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    TTF_Font *fontBtn = TTF_OpenFont(fontPath, 22);
    TTF_Font *fontDisp = TTF_OpenFont(fontPath, 34);
    if (!fontBtn || !fontDisp) {
        std::fprintf(stderr, "TTF_OpenFont: %s\n", TTF_GetError());
        if (fontBtn) TTF_CloseFont(fontBtn);
        if (fontDisp) TTF_CloseFont(fontDisp);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("ac's calc", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H,
                                         SDL_WINDOW_SHOWN);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        TTF_CloseFont(fontBtn);
        TTF_CloseFont(fontDisp);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        TTF_CloseFont(fontBtn);
        TTF_CloseFont(fontDisp);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    std::vector<Button> buttons = buildLayout();
    CalcState calc;
    int mx = 0, my = 0;
    bool mouseDown = false;
    bool running = true;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                running = false;
            if (e.type == SDL_MOUSEMOTION) {
                mx = e.motion.x;
                my = e.motion.y;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                mx = e.button.x;
                my = e.button.y;
                mouseDown = true;
                const Button *b = hitTest(buttons, mx, my);
                if (b) applyButton(calc, *b);
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
                mouseDown = false;

            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE)
                    running = false;
                else if (k == SDLK_c && (e.key.keysym.mod & KMOD_CTRL))
                    calc.clearAll();
                else if (k >= SDLK_0 && k <= SDLK_9) {
                    char d = char('0' + (k - SDLK_0));
                    calc.inputDigit(d);
                } else if (k >= SDLK_KP_0 && k <= SDLK_KP_9) {
                    char d = char('0' + (k - SDLK_KP_0));
                    calc.inputDigit(d);
                } else if (k == SDLK_PERIOD || k == SDLK_KP_PERIOD)
                    calc.inputDot();
                else if (k == SDLK_BACKSPACE)
                    calc.backspace();
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_EQUALS)
                    calc.equals();
                else if (k == SDLK_PLUS || k == SDLK_KP_PLUS)
                    calc.flushOp(BtnKind::Add);
                else if (k == SDLK_MINUS || k == SDLK_KP_MINUS)
                    calc.flushOp(BtnKind::Subtract);
                else if (k == SDLK_ASTERISK || k == SDLK_KP_MULTIPLY)
                    calc.flushOp(BtnKind::Multiply);
                else if (k == SDLK_SLASH || k == SDLK_KP_DIVIDE)
                    calc.flushOp(BtnKind::Divide);
            }
        }

        SDL_SetRenderDrawColor(ren, kBg.r, kBg.g, kBg.b, kBg.a);
        SDL_RenderClear(ren);

        SDL_Rect dr = displayRect();
        fillRect(ren, dr, kDisplayBg);
        strokeRect(ren, dr, kDisplayBorder);

        SDL_Texture *titleTex = makeLabel(ren, fontBtn, "ac's calc", kTextMuted);
        SDL_Rect titleBox{dr.x + 12, dr.y + 8, dr.w - 24, 22};
        if (titleTex) {
            int tw, th;
            SDL_QueryTexture(titleTex, nullptr, nullptr, &tw, &th);
            SDL_Rect td{dr.x + 12, dr.y + 8, tw, th};
            SDL_RenderCopy(ren, titleTex, nullptr, &td);
            SDL_DestroyTexture(titleTex);
        }

        SDL_Texture *dispTex = makeLabel(ren, fontDisp, calc.display.c_str(), kText);
        SDL_Rect numBox{dr.x, dr.y + 28, dr.w, dr.h - 32};
        drawTexture(ren, dispTex, numBox, false);
        if (dispTex) SDL_DestroyTexture(dispTex);

        for (const auto &b : buttons) {
            bool pressed = mouseDown && hitTest(buttons, mx, my) == &b;
            drawButtonFace(ren, b, mx, my, pressed);
            SDL_Color fg = b.eqStyle ? kBtnEqText : kText;
            SDL_Texture *t = makeLabel(ren, fontBtn, b.label, fg);
            drawTexture(ren, t, b.rect, true);
            if (t) SDL_DestroyTexture(t);
        }

        SDL_RenderPresent(ren);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_CloseFont(fontBtn);
    TTF_CloseFont(fontDisp);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
