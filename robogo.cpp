/*
 * name: RoboGo
 * version: 1.0.0
 * author: VisualGMQ
 * data: 2021/11/08
 *
 * compile:
 *   g++ main.cpp -o RoboGo -std=c++17
 *
 * note:
 *   This is a console game, so make sure your console's size bigger than 80x25.
 *   Or you can change `DefaultCanvaWidth` and `DefaultCanvaHeight` to define your own size.
 *   Press CTRL-C to exit the game(or play it until game over).
 *
 * description:
 *   RoboGo is a game that you lead the robot(@) to avoid bullets, and live as long as you can.
 *   You write your commands into `./robocmd.txt`,
 *   then this game will read `./robocmd.txt`(if not exists, it will generate one, which include some help text and an example) and execute your commands.
 *   Commands are executed in a loop, so you needn't write your commands duplicately.
 *
 *   If bullets collide on your robot, you die.
 *   The difficulty will increase by turn increasing.
 */
#include <iostream>
#include <thread>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <random>
#include <string_view>
#include <chrono>
#include <cassert>

/***************************
 * some alias and constexpr
 **************************/

template <typename T>
using Unique = std::unique_ptr<T>;

template <typename T>
using Ref = std::shared_ptr<T>;

constexpr int DefaultCanvaWidth = 80;
constexpr int DefaultCanvaHeight = 22;

bool clsClear = true;

/*****************************************
 * some math function and math structures
*****************************************/

struct Size {
    int w;
    int h;
};

struct Point {
    int x;
    int y;

    bool operator==(const Point& o) {
        return o.x == x && o.y == y;
    }

};

struct Rect {
    Point pos;
    Size size;
};


/*****************
 * some symbols
*****************/

constexpr char SYM_PLAYER = '@';
constexpr char SYM_BULLET_LEFT = '<';
constexpr char SYM_BULLET_RIGHT = '>';
constexpr char SYM_BULLET_UP = '^';
constexpr char SYM_BULLET_DOWN = 'v';

/**************************
 * surface: save char
 *************************/

struct BoxStyle {
    char vLine;
    char hLine;
    char corner;
};

constexpr BoxStyle BlockBox = { '#', '#', '#'};
constexpr BoxStyle WallBox = {'|', '-', '.'};

class Surface {
public:
    Surface(): Surface({DefaultCanvaWidth, DefaultCanvaHeight}) {}

    Surface(const Size& size): size_(size) {
        data_.resize(size.w * size.h, ' ');
    }

    const Size& GetSize() const { return size_; }

    char GetChar(const Point& pos) {
        if (pos.x >= 0 && pos.y >= 0 &&
            pos.x < size_.w && pos.y < size_.h) {
            return data_.at(pos.x + pos.y * size_.w);
        }
        return -1;
    }

    void Clear() {
        for (int i = 0; i < data_.size(); i++)
            data_[i] = ' ';
    }

    void DrawChar(const Point& pos, char c) {
        if (pos.x >= 0 && pos.y >= 0 &&
            pos.x < size_.w && pos.y < size_.h) {
            data_[pos.x + pos.y * size_.w] = c;
        }
    }

    void DrawVLine(int x, int y1, int y2, char c = '|') {
        y1 = std::min(y1, y2);
        y2 = std::max(y1, y2);
        while (y1 <= y2) {
            DrawChar({x, y1}, c);
            y1 ++;
        }
    }

    void DrawHLine(int y, int x1, int x2, char c = '-') {
        x1 = std::min(x1, x2);
        x2 = std::max(x1, x2);
        while (x1 <= x2) {
            DrawChar({x1, y}, c);
            x1 ++;
        }
    }

    void DrawBox(const Rect& rect, const BoxStyle& style) {
        DrawHLine(rect.pos.y, rect.pos.x, rect.pos.x + rect.size.w - 1, style.hLine);
        DrawHLine(rect.pos.y + rect.size.h, rect.pos.x, rect.pos.x + rect.size.w - 1, style.hLine);
        DrawVLine(rect.pos.x, rect.pos.y , rect.pos.y + rect.size.h - 1, style.vLine);
        DrawVLine(rect.pos.x + rect.size.w, rect.pos.y , rect.pos.y + rect.size.h - 1, style.vLine);

        DrawChar(rect.pos, style.corner);
        DrawChar({rect.pos.x + rect.size.w, rect.pos.y}, style.corner);
        DrawChar({rect.pos.x, rect.pos.y + rect.size.h}, style.corner);
        DrawChar({rect.pos.x + rect.size.w, rect.pos.y + rect.size.h}, style.corner);
    }

private:
    Size size_;
    std::vector<char> data_;
};

/*****************
 * command paser
*****************/

enum CmdType {
    INVALID = 0,

    MOVE_RIGHT,
    MOVE_LEFT,
    MOVE_UP,
    MOVE_DOWN,

    REST,

    EXIT_GAME,
};

struct CmdMove {
    int num;
};

struct CmdRest {
    int num;
};

struct Cmd {
    CmdType type = INVALID;
    union {
        CmdMove move;
        CmdRest rest;
    };
};

Cmd ParserCmd(const std::string&);
std::vector<Cmd> ReadCmdFromFile(const std::string& filename);

/************
 * some func
 ***********/

void ClearScreen();
void UpdateScreen(Surface* surface);
void _detectClearWay();
std::vector<std::string_view> SplitStrBySpace(const std::string&);
int RandInt(int low, int high);
void DebugPrintCmd(const Cmd&);

void Init();

/******************
 * some unittests
 *****************/

void TestCmdParser() {
    auto cmd = ParserCmd("right 30");
    assert(cmd.type == MOVE_RIGHT);
}


/****************
 * game related
 ***************/

Unique<Surface> surface(new Surface);

class Robo {
public:
    Point pos = {0, 0};

    void Draw() {
        surface->DrawChar(pos, SYM_PLAYER);
    }
};

Robo player;

void GameLoop();
void DealCmd(Cmd&);

bool NextCmd = false;

bool ShouldExit = false;
std::vector<Cmd> CmdList;

int TurnCount = 0;

struct Bullet {
    enum Direction {
        LEFT = 0,
        RIGHT,
        UP,
        DOWN
    } direction;
    bool valid = false;
    Point pos;
};

class BulletCollection {
public:
    BulletCollection() {
        bullets_.resize(20);
    }

    void GenNewBullet() {
        Bullet b;
        b.valid = true;
        b.pos = {0, 0};
        b.direction = static_cast<Bullet::Direction>(RandInt(0, 3));
        if (b.direction == Bullet::LEFT ||
            b.direction == Bullet::RIGHT) {
            b.pos.y = RandInt(1, surface->GetSize().h - 2);
            if (b.direction == Bullet::LEFT)
                b.pos.x = surface->GetSize().w;
        } else {
            b.pos.x = RandInt(1, surface->GetSize().w - 2);
            if (b.direction == Bullet::UP)
                b.pos.y = surface->GetSize().h;
        }

        bullets_[findValidSlot()] = b;
    }

    void Update() {
        for (auto& b : bullets_) {
            if (b.pos == player.pos) {
                ShouldExit = true;
            }
            switch (b.direction) {
                case Bullet::LEFT:
                    surface->DrawChar(b.pos, SYM_BULLET_LEFT);
                    b.pos.x --;
                    break;
                case Bullet::RIGHT:
                    surface->DrawChar(b.pos, SYM_BULLET_RIGHT);
                    b.pos.x ++;
                    break;
                case Bullet::UP:
                    surface->DrawChar(b.pos, SYM_BULLET_UP);
                    b.pos.y --;
                    break;
                case Bullet::DOWN:
                    surface->DrawChar(b.pos, SYM_BULLET_DOWN);
                    b.pos.y ++;
                    break;
            }
            if (b.pos.x <= 0 || b.pos.x >= surface->GetSize().w ||
                b.pos.y <= 0 || b.pos.y >= surface->GetSize().h) {
                b.valid = false;
            }
            if (b.pos == player.pos) {
                ShouldExit = true;
            }
        }
    }

private:
    std::vector<Bullet> bullets_;

    int findValidSlot() {
        for (int i = 0; i < bullets_.size(); i++)
            if (!bullets_[i].valid)
                return i;
        bullets_.resize(bullets_.size() * 2);
        return bullets_.size() / 2;
    }
};

BulletCollection BulCollection;

/****************
 * main function
 ***************/

int main(int argc, char** argv) {
    Init();
    GameLoop();
    return 0;
}

/****************************
 * function implementations
****************************/

int RandInt(int low, int high) {
    std::random_device d;
    std::uniform_int_distribution<int> dist(low, high);
    return dist(d);
}

void ClearScreen() {
    if (clsClear)
        system("cls");
    else
        system("clear");
}

void _detectClearWay() {
    if (system("clear") == 0) {
        clsClear = false;
    }
}

void UpdateScreen(Surface* surface) {
    for (int y = 0; y < surface->GetSize().h; y++) {
        for (int x = 0; x < surface->GetSize().w; x++) {
            if (x < DefaultCanvaWidth && y < DefaultCanvaHeight) {
                std::putchar(surface->GetChar({x, y}));
            }
        }
        std::cout << std::endl;
    }
}

void Init() {
    _detectClearWay();
    player.pos.x = surface->GetSize().w / 2;
    player.pos.y = surface->GetSize().h / 2;
    CmdList = ReadCmdFromFile("robocmd.txt");
}

std::vector<std::string_view> SplitStrBySpace(const std::string& s) {
    std::vector<std::string_view> result;
    const char* ptr = s.data();
    const char* prev = ptr;
    int len = 0;
    while (*ptr != '\0') {
        if (*ptr != ' ') {
            len ++;
            ptr ++;
        } else {
            result.push_back(std::string_view(prev, len));
            len = 0;
            while (*ptr == ' ' && *ptr != '\0')
                ptr ++;
            prev = ptr;
        }
    }
    if (ptr != prev) {
        result.push_back(std::string_view(prev, len));
    }
    return result;
}

Cmd ParserCmd(const std::string& s) {
    Cmd cmd;
    auto views = SplitStrBySpace(s);
    if (views.size() == 2) {
        if (views[0] == "right") {
            cmd.type = MOVE_RIGHT;
            cmd.move.num = atoi(views[1].data());
        } else if (views[0] == "left") {
            cmd.type = MOVE_LEFT;
            cmd.move.num = atoi(views[1].data());
        } else if (views[0] == "up") {
            cmd.type = MOVE_UP;
            cmd.move.num = atoi(views[1].data());
        } else if (views[0] == "down") {
            cmd.type = MOVE_DOWN;
            cmd.move.num = atoi(views[1].data());
        } else if (views[0] == "rest") {
            cmd.type = REST;
            cmd.move.num = atoi(views[1].data());
        }
    }
    if (views.size() == 1) {
        if (views[0] == "exit")
            cmd.type = EXIT_GAME;
    }
    return cmd;
}

std::vector<Cmd> ReadCmdFromFile(const std::string& filename) {
    std::ifstream file(filename);
    char buf[64] = {0};
    std::vector<Cmd> result;

    if (!file.is_open()) {
        std::ofstream ofile(filename);
        ofile << "# this is comment" << std::endl
              << "# commands:" << std::endl
              << "#    right <value>: goto right <value> turn" << std::endl
              << "#    left <value>:  goto left <value> turn" << std::endl
              << "#    up <value>:    goto up <value> turn" << std::endl
              << "#    down <value>:  goto down <value> turn" << std::endl
              << "#    rest <value>:  rest <value> turn" << std::endl
              << std::endl
              << "# this is an example:" << std::endl
              << std::endl
              << "right 30" << std::endl
              << "left 30" << std::endl;
        std::cout << "No robocmd.txt! Please look ./robocmd.txt to program your own logic." << std::endl;
        exit(1);
    } else {
        while (!file.eof()) {
            file.getline(buf, sizeof(buf));
            auto cmd = ParserCmd(buf);
            if (cmd.type != INVALID)
                result.push_back(ParserCmd(buf));
        }
    }

    if (result.empty()) {
        std::cout << "no command in robocmd.txt" << std::endl;
        ShouldExit = true;
    }

    return result;
}

void DebugPrintCmd(const Cmd& cmd) {
    switch (cmd.type) {
        case MOVE_LEFT:
            std::cout << "move left " << cmd.move.num << std::endl;
            break;
        case MOVE_RIGHT:
            std::cout << "move right" << cmd.move.num << std::endl;
            break;
        case MOVE_UP:
            std::cout << "move up" << cmd.move.num << std::endl;
            break;
        case MOVE_DOWN:
            std::cout << "move down" << cmd.move.num << std::endl;
            break;
        case REST:
            std::cout << "rest" << cmd.rest.num << std::endl;
            break;
        case EXIT_GAME:
            std::cout << "exit game" << std::endl;
            break;
        default:
            std::cout << "invalid command" << std::endl;
    }
}

void DealCmd(Cmd& cmd) {
    switch (cmd.type) {
        case MOVE_RIGHT:
            player.pos.x ++;
            cmd.move.num --;
            if (cmd.move.num == 0)
                NextCmd = true;
            break;
        case MOVE_LEFT:
            player.pos.x --; 
            cmd.move.num --;
            if (cmd.move.num == 0)
                NextCmd = true;
            break;
        case MOVE_UP:
            player.pos.y --; 
            cmd.move.num --;
            if (cmd.move.num == 0)
                NextCmd = true;
            break;
        case MOVE_DOWN:
            player.pos.y ++; 
            cmd.move.num --;
            if (cmd.move.num == 0)
                NextCmd = true;
            break;
        case REST:
            cmd.rest.num --;
            if (cmd.move.num == 0)
                NextCmd = true;
            break;
        case EXIT_GAME:
            ShouldExit = true;
            break;
    }
    if (player.pos.x > surface->GetSize().w - 1)
        player.pos.x = 0;
    if (player.pos.y > surface->GetSize().h - 1)
        player.pos.y = 0;
    if (player.pos.x < 0)
        player.pos.x = surface->GetSize().w - 1;
    if (player.pos.y < 0)
        player.pos.y = surface->GetSize().h - 1;
}

void GameLoop() {
    int cmd_idx = 0;
    std::vector<Cmd> cmds = CmdList;
    int bulletGenCondition = 30;

    while (!ShouldExit) {
        ClearScreen();
        surface->Clear();
        surface->DrawBox({0, 0, surface->GetSize().w - 1, surface->GetSize().h - 1}, WallBox);
        if (TurnCount > 20 && TurnCount <= 40) {
            bulletGenCondition = 40;
        }
        if (TurnCount > 40 && TurnCount <= 60) {
            bulletGenCondition = 50;
        }
        if (TurnCount > 60 && TurnCount <= 80) {
            bulletGenCondition = 60;
        }
        if (TurnCount > 80 && TurnCount <= 100) {
            bulletGenCondition = 70;
        }
        if (TurnCount > 100 && TurnCount <= 120) {
            bulletGenCondition = 80;
        }
        if (TurnCount > 120 && TurnCount <= 140) {
            bulletGenCondition = 90;
        }
        if (RandInt(0, 100) < bulletGenCondition) {
            BulCollection.GenNewBullet();
        }
        BulCollection.Update();
        player.Draw();
        UpdateScreen(surface.get());

        if (NextCmd) {
            cmd_idx ++;
            NextCmd = false;
        }
        if (cmd_idx >= cmds.size()) {
            cmd_idx = 0;
            cmds = CmdList;
        }

        std::cout << TurnCount << " turns.  To exit, press CTRL-C a long time" << std::endl;
        DealCmd(cmds.at(cmd_idx));
        TurnCount ++;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    ClearScreen();
    std::cout << "Game Over, you survived " << TurnCount << " turns!" << std::endl;
}
