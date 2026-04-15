#ifndef FLAPPY_HPP
#define FLAPPY_HPP
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <EEPROM.h>
#include "screen.hpp"
#include "touch.hpp"

// 游戏状态
#define GAME_STATE_READY 0
#define GAME_STATE_PLAYING 1
#define GAME_STATE_GAMEOVER 2

// 鸟的属性
#define BIRD_WIDTH 20
#define BIRD_HEIGHT 20
#define BIRD_X_POS 50
#define GRAVITY 0.5
#define JUMP_STRENGTH -5

// 管道属性
#define PIPE_WIDTH 40
#define PIPE_GAP_BASE 65
#define PIPE_GAP_MIN 60
#define PIPE_GAP_MAX 70
#define PIPE_SPEED 2
#define PIPE_SPACING 160

// 关闭按钮（右上角，仅绘制，功能已移除）
#define CLOSE_BTN_SIZE 32
#define CLOSE_BTN_MARGIN 10

#define BTN_LONG_PUSH_T 1000
#define BUTTON_PIN 0
#define BUTTON_TRIG_LEVEL LOW

#define TOP_BAR_HEIGHT 15

static int history_score_flappy = 0;

inline void save_flappy_hiscore() {
    EEPROM.write(17, (uint8_t)(history_score_flappy & 0xFF));
    EEPROM.write(18, (uint8_t)((history_score_flappy >> 8) & 0xFF));
    EEPROM.commit();
}

inline void load_flappy_hiscore() {
    int val = EEPROM.read(17) | (EEPROM.read(18) << 8);
    if (val == 0xFFFF) {
        history_score_flappy = 0;
        save_flappy_hiscore();
    } else {
        history_score_flappy = val;
    }
}

class FlappyBirdGame {
private:
    TFT_eSPI* tft;
    TFT_eSprite* canvas;
    int gameState;
    int lastScoreRendered;
    int lastGameState;
    float birdAngle;

    float birdY;
    float birdVelocity;

    struct Pipe {
        int x;
        int gapY;
        int gapHeight;
        int lastX;
        bool passed;
    };

    void drawRotatedRect(TFT_eSprite* dst, int cx, int cy, int w, int h, float angleDeg, uint16_t fillColor, uint16_t outlineColor) {
        float rad = angleDeg * 3.14159265f / 180.0f;
        float c = cos(rad);
        float s = sin(rad);
        float hw = w / 2.0f;
        float hh = h / 2.0f;
        float rx[4];
        float ry[4];
        float corners[4][2] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        for (int i = 0; i < 4; i++) {
            float x = corners[i][0];
            float y = corners[i][1];
            float xr = x * c - y * s;
            float yr = x * s + y * c;
            rx[i] = cx + xr;
            ry[i] = cy + yr;
        }
        dst->fillTriangle((int)rx[0], (int)ry[0], (int)rx[1], (int)ry[1], (int)rx[2], (int)ry[2], fillColor);
        dst->fillTriangle((int)rx[0], (int)ry[0], (int)rx[2], (int)ry[2], (int)rx[3], (int)ry[3], fillColor);
        dst->drawLine((int)rx[0], (int)ry[0], (int)rx[1], (int)ry[1], outlineColor);
        dst->drawLine((int)rx[1], (int)ry[1], (int)rx[2], (int)ry[2], outlineColor);
        dst->drawLine((int)rx[2], (int)ry[2], (int)rx[3], (int)ry[3], outlineColor);
        dst->drawLine((int)rx[3], (int)ry[3], (int)rx[0], (int)ry[0], outlineColor);
    }

    Pipe pipes[5];
    int pipeCount;
    int score;
    unsigned long lastPipeTime;

public:
    FlappyBirdGame(TFT_eSPI* display) {
        tft = display;
        canvas = new TFT_eSprite(tft);
        canvas->setColorDepth(8);
        if (canvas->createSprite(screenWidth, screenHeight - TOP_BAR_HEIGHT) == nullptr) {
            Serial.println("Error: Failed to create canvas sprite!");
        }
        lastScoreRendered = -1;
        lastGameState = -1;
        birdAngle = 0.0f;
        reset();
    }

    ~FlappyBirdGame(){
        if (canvas){
            canvas->deleteSprite();
            delete canvas;
            canvas = nullptr;
        }
    }

    void reset() {
        tft->fillScreen(TFT_BLACK);
        tft->fillRect(0, 0, screenWidth, TOP_BAR_HEIGHT, TFT_BLACK);
        lastGameState = -1;
        gameState = GAME_STATE_READY;
        birdY = screenHeight / 2;
        birdVelocity = 0;
        score = 0;
        pipeCount = 0;
        lastPipeTime = 0;
        for (int i = 0; i < 5; i++) {
            pipes[i].x = -100;
            pipes[i].gapHeight = PIPE_GAP_BASE;
            pipes[i].gapY = screenHeight / 2;
            pipes[i].passed = false;
        }
        birdAngle = 0.0f;
    }

    void update() {
        switch (gameState) {
            case GAME_STATE_READY:
                updateReady();
                break;
            case GAME_STATE_PLAYING:
                updatePlaying();
                break;
            case GAME_STATE_GAMEOVER:
                updateGameOver();
                break;
        }
        render();
    }

    void updateReady() {
        static bool last_touched = false;
        bool touching = touch.tp.touching;
        if (touching) {
            last_touched = true;
        } else {
            if (last_touched) {
                gameState = GAME_STATE_PLAYING;
                lastPipeTime = millis();
                tft->fillScreen(TFT_BLACK);
            }
            last_touched = false;
        }
        button_loop();
    }

    void updatePlaying() {
        birdVelocity += GRAVITY;
        birdY += birdVelocity;
        {
            float desiredAngle = birdVelocity * 15.0f;
            if (desiredAngle < -60.0f) desiredAngle = -60.0f;
            if (desiredAngle > 90.0f) desiredAngle = 90.0f;
            birdAngle += (desiredAngle - birdAngle) * 0.12f;
        }

        static bool last_touched = false;
        bool touching = touch.tp.touching;
        if (touching) {
            if (!last_touched) {
                birdVelocity = JUMP_STRENGTH;
                birdAngle = -60.0f;
            }
            last_touched = true;
        } else {
            last_touched = false;
        }
        button_loop();

        updatePipes();
        checkCollisions();

        if (birdY < 0 || birdY > screenHeight - BIRD_HEIGHT) {
            gameState = GAME_STATE_GAMEOVER;
            tft->fillScreen(TFT_BLACK);
        }
    }

    void updateGameOver() {
        static bool last_touched = false;
        bool touching = touch.tp.touching;
        if (touching) {
            last_touched = true;
        } else {
            if (last_touched) {
                reset();
            }
            last_touched = false;
        }
        button_loop();
    }

    void updatePipes() {
        for (int i = 0; i < pipeCount; i++) {
            pipes[i].x -= PIPE_SPEED;
            if (!pipes[i].passed && pipes[i].x + PIPE_WIDTH < BIRD_X_POS) {
                pipes[i].passed = true;
                score++;
            }
            if (pipes[i].x < -PIPE_WIDTH) {
                for (int j = i; j < pipeCount - 1; j++) {
                    pipes[j] = pipes[j+1];
                }
                pipeCount--;
                i--;
            }
        }
        if ((pipeCount == 0) || (pipeCount < 5 && pipes[pipeCount-1].x < screenWidth - PIPE_SPACING)) {
            pipes[pipeCount].x = screenWidth;
            pipes[pipeCount].gapHeight = random(PIPE_GAP_MIN, PIPE_GAP_MAX);
            int margin = 20;
            int minY = TOP_BAR_HEIGHT + margin;
            int maxY = screenHeight - pipes[pipeCount].gapHeight - margin;
            if (maxY <= minY) {
                pipes[pipeCount].gapY = minY;
            } else {
                pipes[pipeCount].gapY = random(minY, maxY);
            }
            pipes[pipeCount].passed = false;
            pipeCount++;
        }
    }

    void checkCollisions() {
        for (int i = 0; i < pipeCount; i++) {
            if (BIRD_X_POS + BIRD_WIDTH > pipes[i].x && BIRD_X_POS < pipes[i].x + PIPE_WIDTH) {
                if (birdY < pipes[i].gapY || birdY + BIRD_HEIGHT > pipes[i].gapY + pipes[i].gapHeight) {
                    if (score >= history_score_flappy) {
                        history_score_flappy = score;
                        save_flappy_hiscore();
                    }
                    gameState = GAME_STATE_GAMEOVER;
                    tft->fillScreen(TFT_BLACK);
                    return;
                }
            }
        }
    }

    void render() {
        if (!canvas) return;
        if (gameState == GAME_STATE_PLAYING) {
            canvas->fillSprite(TFT_BLACK);
            for (int i = 0; i < pipeCount; i++) {
                int topPipeHeight = pipes[i].gapY - TOP_BAR_HEIGHT;
                if (topPipeHeight > 0) {
                    canvas->fillRect(pipes[i].x, 0, PIPE_WIDTH, topPipeHeight, TFT_GREEN);
                }
                int lowerPipeY = pipes[i].gapY + pipes[i].gapHeight;
                int lowerPipeYc = lowerPipeY - TOP_BAR_HEIGHT;
                int lowerPipeHeight = screenHeight - lowerPipeY;
                if (lowerPipeHeight > 0) {
                    canvas->fillRect(pipes[i].x, lowerPipeYc, PIPE_WIDTH, lowerPipeHeight, TFT_GREEN);
                }
            }
            int birdYc = (int)birdY - TOP_BAR_HEIGHT;
            int birdCx = BIRD_X_POS + (BIRD_WIDTH / 2);
            int birdCy = birdYc + (BIRD_HEIGHT / 2);
            drawRotatedRect(canvas, birdCx, birdCy, BIRD_WIDTH, BIRD_HEIGHT, birdAngle, TFT_YELLOW, TFT_RED);

            int btnX = screenWidth - CLOSE_BTN_SIZE - CLOSE_BTN_MARGIN;
            int btnY = CLOSE_BTN_MARGIN;
            int btnYc = btnY - TOP_BAR_HEIGHT;
            if (btnYc < 0) btnYc = 0;
            canvas->fillRect(btnX, btnYc, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, TFT_BLACK);
            canvas->drawLine(btnX + 4, btnYc + 4, btnX + CLOSE_BTN_SIZE - 5, btnYc + CLOSE_BTN_SIZE - 5, TFT_RED);
            canvas->drawLine(btnX + CLOSE_BTN_SIZE - 5, btnYc + 4, btnX + 4, btnYc + CLOSE_BTN_SIZE - 5, TFT_RED);

            canvas->pushSprite(0, TOP_BAR_HEIGHT);

            if (score != lastScoreRendered || lastGameState != gameState) {
                if (score > history_score_flappy) {history_score_flappy = score;}
                tft->startWrite();
                tft->fillRect(0, 0, screenWidth, TOP_BAR_HEIGHT, TFT_BLACK);
                tft->setTextSize(1);
                tft->setTextColor(TFT_WHITE, TFT_BLACK);
                tft->setCursor(50, 1);
                tft->print("Score:");
                tft->setCursor(100, 1);
                tft->print(score);
                tft->setCursor(160, 1);
                tft->print("Hiscore:");
                tft->setCursor(220, 1);
                tft->print(history_score_flappy);
                tft->endWrite();
                lastScoreRendered = score;
                lastGameState = gameState;
            }
            return;
        }

        if (lastGameState != gameState) {
            canvas->fillSprite(TFT_BLACK);
            for (int i = 0; i < pipeCount; i++) {
                int topPipeHeight = pipes[i].gapY - TOP_BAR_HEIGHT;
                if (topPipeHeight > 0) {
                    canvas->fillRect(pipes[i].x, 0, PIPE_WIDTH, topPipeHeight, TFT_GREEN);
                }
                int lowerPipeY = pipes[i].gapY + pipes[i].gapHeight;
                int lowerPipeYc = lowerPipeY - TOP_BAR_HEIGHT;
                int lowerPipeHeight = screenHeight - lowerPipeY;
                if (lowerPipeHeight > 0) {
                    canvas->fillRect(pipes[i].x, lowerPipeYc, PIPE_WIDTH, lowerPipeHeight, TFT_GREEN);
                }
            }
            int birdYc = (int)birdY - TOP_BAR_HEIGHT;
            int birdCx = BIRD_X_POS + (BIRD_WIDTH / 2);
            int birdCy = birdYc + (BIRD_HEIGHT / 2);
            drawRotatedRect(canvas, birdCx, birdCy, BIRD_WIDTH, BIRD_HEIGHT, birdAngle, TFT_YELLOW, TFT_RED);
            int btnX = screenWidth - CLOSE_BTN_SIZE - CLOSE_BTN_MARGIN;
            int btnY = CLOSE_BTN_MARGIN;
            int btnYc = btnY - TOP_BAR_HEIGHT;
            if (btnYc < 0) btnYc = 0;
            canvas->fillRect(btnX, btnYc, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, TFT_BLACK);
            canvas->drawLine(btnX + 4, btnYc + 4, btnX + CLOSE_BTN_SIZE - 5, btnYc + CLOSE_BTN_SIZE - 5, TFT_RED);
            canvas->drawLine(btnX + CLOSE_BTN_SIZE - 5, btnYc + 4, btnX + 4, btnYc + CLOSE_BTN_SIZE - 5, TFT_RED);
            canvas->pushSprite(0, TOP_BAR_HEIGHT);

            tft->startWrite();
            tft->fillRect(0, 0, screenWidth, TOP_BAR_HEIGHT, TFT_BLACK);
            tft->setTextSize(1);
            tft->setTextColor(TFT_WHITE, TFT_BLACK);
            tft->setCursor(50, 1);
            tft->print("Score:");
            tft->setCursor(100, 1);
            tft->print(score);
            tft->setCursor(160, 1);
            tft->print("Hiscore:");
            tft->setCursor(220, 1);
            tft->print(history_score_flappy);

            tft->setTextSize(3);
            if (gameState == GAME_STATE_READY) {
                tft->setCursor(16, 100);
                tft->print("Touch to Start");
            } else if (gameState == GAME_STATE_GAMEOVER) {
                tft->setCursor(60, 100);
                tft->print("Game Over");
            }
            tft->endWrite();

            lastScoreRendered = score;
            lastGameState = gameState;
        }
    }

    void _func_button_pushed(){
        switch (gameState) {
            case GAME_STATE_READY:
                gameState = GAME_STATE_PLAYING;
                lastPipeTime = millis();
                tft->fillScreen(TFT_BLACK);
                break;
            case GAME_STATE_PLAYING:
                birdVelocity = JUMP_STRENGTH;
                break;
            case GAME_STATE_GAMEOVER:
                reset();
                break;
        }
    }
    void _func_button_long_pushed(){
        // 原 logoff 已移除，长按暂不执行退出
    }
    void button_loop(){
        static unsigned long btn_pushed_start_time =  0;
        static bool btn_pushed = false;
        static bool btn_long_pushed = false;
        if (digitalRead(BUTTON_PIN) == BUTTON_TRIG_LEVEL){
            if (millis() - btn_pushed_start_time >= BTN_LONG_PUSH_T){
                if (!btn_long_pushed){
                    _func_button_long_pushed();
                    btn_long_pushed = true;
                }
            }
            delay(5);
            if (digitalRead(BUTTON_PIN) == BUTTON_TRIG_LEVEL){btn_pushed=true;}
        }else{
            btn_pushed_start_time = millis();
            if (btn_pushed) {
                if (!btn_long_pushed){_func_button_pushed();}
            }
            btn_pushed=false;
            btn_long_pushed = false;
        }
    }
};

#endif
