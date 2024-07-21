// 第１６回ＵＥＣ杯コンピュータ囲碁大会用きふわらべ
//
// 今大会でのコンセプト：
// 
//      コンパス　きふわらべ
// 
//          １つ前の自分の手と、さっきの相手の着手の２つの石の距離分コンパスを広げ、
//          相手の着手にコンパスの針を突き立てて、円を描く。
//          その円状に石を置く。
// 
// ソースのベース：
//      CgfGoban.exe用の思考ルーチンのサンプル
//      2005/06/04 - 2005/07/15 山下 宏
//      乱数で手を返すだけです。
//
// CgfGoban.exe の方が 16bit？
//      ----> x86 でビルドしてみる
//
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// math.h の中の M_PI を使えるようにする定義
#define _USE_MATH_DEFINES
#include <math.h>
//#include <cmath> // インクルードするとコンパイラーがエラー出す？

#include <windows.h>
//#include <tuple>    // C++11 からタプルが使えるそうだ。でもこれＣ言語



// インクルードでパスが見つからなかったので、 cgfthink.h の内容を、ここへ埋め込んだ
// アプリケーションから呼ばれる関数の宣言

#define DLL_EXPORT	__declspec( dllexport )

// 思考ルーチン。本体から現在の手数とそれまでの棋譜が入った状態で呼ばれる。
// 手の座標を返す。PASSの場合0を。
// また、終局処理の場合は、終局判断の結果を返す。
DLL_EXPORT int cgfgui_thinking(
    int init_board[],	// 初期盤面（置碁の場合は、ここに置石が入る）
    int kifu[][3],		// 棋譜  [][0]...座標、[][1]...石の色、[][2]...消費時間（秒)
    int tesuu,			// 手数
    int black_turn,		// 手番(黒番...1、白番...0)
    int board_size,		// 盤面のサイズ
    double komi,		// コミ
    int endgame_type,	// 0...通常の思考、1...終局処理、2...図形を表示、3...数値を表示。
    int endgame_board[]	// 終局処理の結果を代入する。
);

// 対局開始時に一度だけ呼ばれます。
DLL_EXPORT void cgfgui_thinking_init(int* ptr_stop_thinking);

// 対局終了時に一度だけ呼ばれます。
DLL_EXPORT void cgfgui_thinking_close(void);


#define BLACK 1
#define WHITE 2
#define WAKU  3		// 盤外

// 現在局面で何をするか、を指定
enum GameType {
    GAME_MOVE,			// 通常の手
    GAME_END_STATUS,	// 終局処理
    GAME_DRAW_FIGURE,	// 図形を描く
    GAME_DRAW_NUMBER 	// 数値を書く
};

// 盤面、石の上に描く記号
// (形 | 色) で指定する。黒で四角を描く場合は (FIGURE_SQUARE | FIGURE_BLACK)
enum FigureType {
    FIGURE_NONE,			// 何も描かない
    FIGURE_TRIANGLE,		// 三角形
    FIGURE_SQUARE,			// 四角
    FIGURE_CIRCLE,			// 円
    FIGURE_CROSS,			// ×
    FIGURE_QUESTION,		// "？"の記号	
    FIGURE_HORIZON,			// 横線
    FIGURE_VERTICAL,		// 縦線
    FIGURE_LINE_LEFTUP,		// 斜め、左上から右下
    FIGURE_LINE_RIGHTUP,	// 斜め、左下から右上
    FIGURE_BLACK = 0x1000,	// 黒で描く（色指定)
    FIGURE_WHITE = 0x2000,	// 白で描く	(色指定）
};

// 死活情報でセットする値
// その位置の石が「活」か「死」か。不明な場合は「活」で。
// その位置の点が「黒地」「白地」「ダメ」か。
enum GtpStatusType {
    GTP_ALIVE,				// 活
    GTP_DEAD,				// 死
    GTP_ALIVE_IN_SEKI,		// セキで活（未使用、「活」で代用して下さい）
    GTP_WHITE_TERRITORY,	// 白地
    GTP_BLACK_TERRITORY,	// 黒地
    GTP_DAME				// ダメ
};


// サンプルで使用する関数
void PRT(const wchar_t* fmt, ...);	// printf()の代用関数。コンソールに出力。
void PassWindowsSystem(void);	// 一時的にWindowsに制御を渡します。


// 思考中断フラグ。0で初期化されています。
// GUIの「思考中断ボタン」を押された場合に1になります。
int* pThinkStop = NULL;


#define BOARD_MAX ((19+2)*256)			// 19路盤を最大サイズとする

int board[BOARD_MAX];
int check_board[BOARD_MAX];		// 既にこの石を検索した場合は1

int board_size;	// 盤面のサイズ。19路盤では19、9路盤では9

// 左右、上下に移動する場合の動く量
int dir4[4] = { +0x001,-0x001,+0x100,-0x100 };

int g_ishi = 0;	            // 取った石の数(再帰関数で使う)
int g_liberty = 0;	        // 連の呼吸点の数(再帰関数で使う)
int g_last_liberty_z = -1;   // 呼吸点の探索中の最後にスキャンした空点。もし呼吸点が１個の場合、この呼吸点を埋めると石を取り上げることができる

int g_kou_z = 0;	// 次にコウになる位置
int hama[2];	// [0]... 黒が取った石の数, [1]...白が取った石の数
int sg_time[2];	// 累計思考時間

#define UNCOL(x) (3-(x))	// 石の色を反転させる

// move()関数で手を進めた時の結果
enum MoveResult {
    MOVE_SUCCESS,	// 成功
    MOVE_SUICIDE,	// 自殺手
    MOVE_KOU,		// コウ
    MOVE_EXIST,		// 既に石が存在
    MOVE_FATAL		// それ以外
};

// 角度に乱数を入れていないと、同じパターンでハメれるので、乱数を入れておく
#define G_ANGLE_DEGREES_360_SIZE 360
int g_angle_degrees_360[G_ANGLE_DEGREES_360_SIZE];
int g_angle_cursor;

// 19 に √2 を掛けるとおよそ 27。 上にも下にも -27 ～ 27 の数を、振動させながら用意する
#define G_OFFSET_DISTANCE_55_SIZE 55
int offset_distance_55[G_OFFSET_DISTANCE_55_SIZE];

// 19路盤の面積
#define G_BOARD_AREA_19ROBAN 361

// 19路盤の各交点の連Id。整数値の範囲は 0～360。サイズは囲碁盤の面積と同じ。該当なしなら -1
int g_ren_id_by_each_node[G_BOARD_AREA_19ROBAN];

// 連の石の色
int g_color_each_ren[G_BOARD_AREA_19ROBAN];

// 連の石の数
int g_stones_each_ren[G_BOARD_AREA_19ROBAN];

// 連の呼吸点数
int g_liberty_each_ren[G_BOARD_AREA_19ROBAN];



// 関数のプロトタイプ宣言
void count_liberty(int tz);				        // 呼吸点と石の数を調べる
void count_liberty_sub(int tz, int my_color);	// 呼吸点と石の数を調べる再帰関数
int move_one(int z, int color);			        // 1手進める。z ... 座標、color ... 石の色
void print_board(void);					        // 現在の盤面を表示
int get_z(int x, int y);					    // (x,y)を1つの座標に変換
int endgame_status(int endgame_board[]);		// 終局処理
int endgame_draw_figure(int endgame_board[]);	// 図形を描く
int endgame_draw_number(int endgame_board[]);	// 数値を書く(0は表示されない)


// 一時的にWindowsに制御を渡します。
// 思考中にこの関数を呼ぶと思考中断ボタンが有効になります。
// 毎秒30回以上呼ばれるようにするとスムーズに中断できます。
void PassWindowsSystem(void)
{
    MSG msg;

    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);						// keyboard input.
        DispatchMessage(&msg);
    }
}

#define PRT_LEN_MAX 256			// 最大256文字まで出力可
static HANDLE hOutput = INVALID_HANDLE_VALUE;	// コンソールに出力するためのハンドル


// printf()の代用関数。
void PRT(const wchar_t* fmt, ...)
{
    // 可変長引数か？
    va_list ap;

    int len;
    static wchar_t text[PRT_LEN_MAX];
    DWORD nw;

    if (hOutput == INVALID_HANDLE_VALUE) return;
    va_start(ap, fmt);

    len = _vsnwprintf_s(text, PRT_LEN_MAX - 1, _TRUNCATE, fmt, ap);
    va_end(ap);

    if (len < 0 || len >= PRT_LEN_MAX) return;

    WriteConsole(hOutput, text, (DWORD)wcslen(text), &nw, NULL);
}


// ########
// # 主要 #
// ########
// 
// GUI からエンジンが読み込まれたときに一度だけ呼ばれます。対局開始時ではありません
//
DLL_EXPORT void cgfgui_thinking_init(int* ptr_stop_thinking)
{
    // 中断フラグへのポインタ変数。
    // この値が1になった場合は思考を終了してください。
    pThinkStop = ptr_stop_thinking;

    // PRT()情報を表示するためのコンソールを起動する。
    AllocConsole();		// この行をコメントアウトすればコンソールは表示されません。
    SetConsoleTitle(L"CgfgobanDLL Infomation Window");
    hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    PRT(L"デバッグ用の窓です。PRT()関数で出力できます。\n");

    // この下に、メモリの確保など必要な場合のコードを記述してください。

    PRT(L"M_PI:%.10f\n", M_PI);

    // ##########
    // # 角度
    // ##########

    // 360°を８分割してセット
    //
    // 順序を星の書き順になるように工夫
    // 
    //  (7) 225°  (2) 270°  (5) 315°
    //  (4) 180°             (0)   0°
    //  (1) 135°  (6)  90°  (3)  45°
    //
    for (int i = 0; i < 45; i++) {
        g_angle_degrees_360[i * 8 + 0] = i + 0;
        g_angle_degrees_360[i * 8 + 1] = i + 135;
        g_angle_degrees_360[i * 8 + 2] = i + 270;
        g_angle_degrees_360[i * 8 + 3] = i + 45;
        g_angle_degrees_360[i * 8 + 4] = i + 180;
        g_angle_degrees_360[i * 8 + 5] = i + 315;
        g_angle_degrees_360[i * 8 + 6] = i + 90;
        g_angle_degrees_360[i * 8 + 7] = i + 225;
    }

    //// デバッグ表示
    //for (int i = 0; i < G_ANGLE_DEGREES_360_SIZE; i++) {
    //    PRT(L"[angle]  i:%3d  angle:%3d\n", i, g_angle_degrees_360[i]);
    //}

    // ##########
    // # 距離
    // ##########

    // 囲碁は、離して打つ方が良い手が多い
    // 19 に √2 を掛けるとおよそ 27。 まず、初期位置より離していく手を考え、そのあと、初期位置より手前にしていく手を考える
    int i = 0;
    offset_distance_55[i] = 0;
    i++;
    for (int radius = 1; radius < 28; radius++) {
        offset_distance_55[i] = radius;
        i++;
    }
    for (int radius = 1; radius < 28; radius++) {
        offset_distance_55[i] = -radius;
        i++;
    }
}


// 対局終了時に一度だけ呼ばれます。
// メモリの解放などが必要な場合にここに記述してください。
DLL_EXPORT void cgfgui_thinking_close(void)
{
    FreeConsole();
    // この下に、メモリの解放など必要な場合のコードを記述してください。
}


// z を x へ。基数
int get_x(int z)
{
    return z % 256 - 1;
}


// z を y へ。基数
int get_y(int z)
{
    return (z / 256) - 1;
}


// 最後の手に対して、９０°回転したところを返す
int get_mirror_z(int last_z)
{
    int x = get_x(last_z);
    int y = get_y(last_z);

    int new_x = y;          // 基数
    int new_y = 18 - x;     // 基数

    return get_z(new_x, new_y);
}


// 置けるかどうか判定
int can_put(int ret_z) {
    if (board[ret_z] != 0) {
        // 石の上には置けない
        //PRT(L"（x:%2d y:%2d）　石の上には置けない\n", get_x(ret_z), get_y(ret_z));
        return 0;
    }

    // 石の上に置くわけでなければＯＫ。ただし...
    count_liberty(ret_z);

    if (g_liberty == 0) {
        // 呼吸できないところには置けない
        //PRT(L"（x:%2d y:%2d）　呼吸できないところには置けない\n", get_x(ret_z), get_y(ret_z));
        return 0;
    }

    // 置けない理由がないから置ける
    return 1;
}


// 天元か？
int is_tengen(int x, int y) {
    return x == 9 && y == 9;
}

// 大きい数から小さい数を引く
int subtract_small_from_large(int a, int b) {
    if (a <= b) {
        return b - a;
    }

    return a - b;
}


int radians_to_degrees(float radians) {
    return (int)(radians * (180.0f / M_PI));
}


float degrees_to_radians(int degrees) {
    return (float)degrees * (float)M_PI / 180.0f;
}


// 現在局面を作る
void setupCurrentPosition(
    int dll_init_board[],	// 初期盤面
    int dll_board_size		// 盤面のサイズ
)
{
    // 現在局面を棋譜と初期盤面から作る
    for (int i = 0; i < BOARD_MAX; i++) board[i] = dll_init_board[i];	// 初期盤面をコピー
    board_size = dll_board_size;    // 盤サイズをグローバル変数に入れる
    hama[0] = hama[1] = 0;          // アゲハマの数を０にする
    sg_time[0] = sg_time[1] = 0;	// 累計思考時間を０にする
    g_kou_z = 0;                    // コウのマス番号を無しにする
}


// 棋譜の読取
void readKifu(
    int dll_kifu[][3],		// 棋譜
                            // [][0]...座標
                            // [][1]...石の色
                            // [][2]...消費時間（秒)
    int dll_tesuu 			// 手数
)
{
    // 棋譜の読取
    for (int i = 0; i < dll_tesuu; i++) {
        int z = dll_kifu[i][0];	        // 座標、y*256 + x の形で入っている
        int color = dll_kifu[i][1];	    // 石の色
        int t = dll_kifu[i][2];	        // 消費時間
        sg_time[i & 1] += t;

        if (move_one(z, color) != MOVE_SUCCESS) break;
    }
}


// 壁で反射する
int reflection_x_on_the_wall(
    int x
) 
{
    // 例えば x:-3 なら、x:3 にする
    if (x < 0)
    {
        x *= -1;
    }

    // 例えば 20 なら、 19 からはみ出た分を 20 から引く
    if (19 < x) {
        x = x - (x - 19);
    }

    return x;
}


// 壁で反射する
int reflection_y_on_the_wall(
    int y
)
{
    if (y < 0)
    {
        y *= -1;
    }

    // 例えば 20 なら、 19 からはみ出た分を 20 から引く
    if (19 < y) {
        y = y - (y - 19);
    }

    return y;
}


// コウ判定
//
// Returns
// -------
// is_ko
//      0: False
//      1: True
int is_ko(
    int ret_z               // 着手予定座標
)
{
    // コウは無い
    if (g_kou_z == 0) {
        return 0;
    }

    return g_kou_z == ret_z;
}


// 角度を取得
int next_angle_degrees() {
    int angle = g_angle_degrees_360[g_angle_cursor];
    g_angle_cursor = (g_angle_cursor + 1) % G_ANGLE_DEGREES_360_SIZE;
    return angle;
}


// 相手の石を取り上げられる空点があるなら、それを返す。無ければ -1
//
// ただし、石１個を取り上げるのを優先すると、３コウになりやすいので、
// 取り上げる石の最低数を指定できるようにする
//
int find_atari_z(
    int dll_kifu[][3],		// 棋譜
                            // [n][]...手数
                            // [][0]...座標
                            // [][1]...石の色
                            // [][2]...消費時間（秒)
    int dll_tesuu,			// 手数
    int my_color,           // 自分の石の色
    int min_agehama,        // 取り上げる石の最低数
    int max_agehama
)
{
    int old_liberty = g_liberty;
    int old_ishi = g_ishi;

    int max_atari_ishi = 0;
    int atari_z = -1;

    for (int y = 0; y < 19; y++) {
        for (int x = 0; x < 19; x++) {
            int z = get_z(x, y);

            // 相手の石
            if (board[z] == UNCOL(my_color)) {

                // 呼吸点を探索する
                count_liberty(z);

                // アタリだ
                if (g_liberty == 1 && max_atari_ishi < g_ishi && min_agehama <= g_ishi && g_ishi <= max_agehama) {

                    // アテがコウになるなら無視
                    if (is_ko(g_last_liberty_z)) {
                        continue;
                    }

                    max_atari_ishi = g_ishi;
                    atari_z = g_last_liberty_z;
                }
            }
        }
    }

    // Restore
    g_liberty = old_liberty;
    g_ishi = old_ishi;

    return atari_z;
}




// ########
// # 主要 #
// ########
// 
// 思考ルーチン。次の1手を返す。
// 本体から初期盤面、棋譜、手数、手番、盤のサイズ、コミ、が入った状態で呼ばれる。
//
//
DLL_EXPORT int cgfgui_thinking(
    int dll_init_board[],	// 初期盤面
    int dll_kifu[][3],		// 棋譜
                            // [n][]...手数
                            // [][0]...座標
                            // [][1]...石の色
                            // [][2]...消費時間（秒)
    int dll_tesuu,			// 手数
    int dll_black_turn,		// 手番
                            // 0...白番
                            // 1...黒番
    int dll_board_size,		// 盤面のサイズ
    double dll_komi,		// コミ
    int dll_endgame_type,	// 0...通常の思考
                            // 1...終局処理
                            // 2...図形を表示
                            // 3...数値を表示
    int dll_endgame_board[]	// 終局処理の結果を代入する。
)
{
    int my_color;

    if (dll_black_turn == 1) {
        my_color = 1;
    }
    else {
        my_color = 2;
    }

    // 石を置く先に、石が無いか、石の色を確認
    int destination_color;

    // 現在局面を作る
    setupCurrentPosition(dll_init_board, dll_board_size);

    // 棋譜の読取
    readKifu(dll_kifu, dll_tesuu);

#if 0	// 中断処理を入れる場合のサンプル。0を1にすればコンパイルされます。
    for (i = 0; i < 300; i++) {				// 300*10ms = 3000ms = 3秒待ちます。
        PassWindowsSystem();			// 一時的にWindowsに制御を渡します。
        if (*pThinkStop != 0) break;	// 中断ボタンが押された場合。
        Sleep(10);						// 10ms(0.01秒)停止。
    }
#endif

    // 盤に死活を表示するモード（終局処理）
    if (dll_endgame_type == GAME_END_STATUS) return endgame_status(dll_endgame_board);

    // 盤に図形を表示するモード
    if (dll_endgame_type == GAME_DRAW_FIGURE) return endgame_draw_figure(dll_endgame_board);

    // 盤に数値を表示するモード
    if (dll_endgame_type == GAME_DRAW_NUMBER) return endgame_draw_number(dll_endgame_board);

    // 以下、プレイ

    // 第１６回ＵＥＣ杯コンピュータ囲碁大会
    // 
    // 持ち時間３０分　切れ負け
    //      ----> ３０分は１８００秒。
    //
    // 手数上限は４００手
    //      ----> １８００秒を４００で割ると４．５。　１手平均４．５秒使ってしまうと時間切れになる。
    //            １手２秒ぐらいスリープさせても８００秒。約１３分強。これぐらい時間を消費させた方がゆっくり観戦できるのでは？
    //            処理を増やすと、もっと時間消費するかも。
    int sleepSeconds = 0; // for debug
    //int sleepSeconds = 2;
    PRT(L"スリープ %4d 秒", sleepSeconds);
    Sleep(sleepSeconds * 1000);

    PRT(L"[%4d手目]  思考時間：先手=%d秒、後手=%d秒\n", dll_tesuu + 1, sg_time[0], sg_time[1]);



    // ##########
    // # １手目、または０手目
    // ##########
    //
    //      つまり自分（コンピュータ）が初手を打つケース。対局開始時の初期化をするのに使う
    //
    if (dll_tesuu < 2) {
        // 初期化
        g_angle_cursor = 0;

        // 初回に情報表示
        PRT(L"大会情報  盤サイズ:%d  コミ:%.1f\n", dll_board_size, dll_komi);
    }

    // ##########
    // # 石を取り上げられる呼吸点があるなら、優先して置く
    // ##########
    {
        int atari_z = find_atari_z(
            dll_kifu,
            dll_tesuu,
            my_color,
            1, 361);     // 最低でも取り上げる石の数。1 より大きい数字

        if (atari_z != -1) {
            int atari_x = get_x(atari_z);
            int atari_y = get_y(atari_z);

            PRT(L"[%4d手目]  atari_x:%2d  atari_y:%2d  atari_z:%04x  石を取り上げられる空点に打つ\n", dll_tesuu + 1, atari_x, atari_y, atari_z & 0xff);
            return atari_z;
        }
    }


    // ##########
    // # 取り上げられそうな石は、ノビをして逃げたい
    // ##########
    //
    // 呼吸点が１になっている自分の連があれば、その呼吸点に着手したい。
    //      ただし、コウと自殺手を除く
    //
    // シチョウになるリスクは増える
    //
    {
        int atari_me_z = find_atari_z(
            dll_kifu,
            dll_tesuu,
            UNCOL(my_color),
            1, 2);     // 最低でも取り上げる石の数。1 より大きい数字

        if (atari_me_z != -1 && !is_ko(atari_me_z)) {

            // 自殺手でないことを判定
            count_liberty(atari_me_z);
            if (g_liberty != 0) {
                int atari_me_x = get_x(atari_me_z);
                int atari_me_y = get_y(atari_me_z);

                PRT(L"[%4d手目]  atari_me_x:%2d  atari_me_y:%2d  atari_me_z:%04x  取り上げられそうな石は、ノビをして逃げたい\n", dll_tesuu + 1, atari_me_x, atari_me_y, atari_me_z & 0xff);
                return atari_me_z;
            }
        }
    }

    // ##########
    // # １手目
    // ##########
    //
    //      つまり自分（コンピュータ）が初手を打つケース
    //
    if (dll_tesuu == 0) {
        // 天元に打つ
        PRT(L"[%4d手目]  天元に打つ\n", dll_tesuu + 1);

        // 19路盤での天元
        return get_z(9, 9);
    }

    // 以下、２手目以降

    // １手前の相手の手
    int last_z = dll_kifu[dll_tesuu - 1][0];

    // 相手がＰａｓｓなら自分もＰａｓｓ
    if (last_z == 0) {
        PRT(L"[%4d手目]  pass  相手がパスしたから自分もパス\n", dll_tesuu + 1);
        return 0;
    }

    int last_x = get_x(last_z);
    int last_y = get_y(last_z);
    //PRT(L"[%4d手目]  last_masu:(%2d, %2d)  last_z:%04x\n", dll_tesuu + 1, last_x + 1, last_y + 1, last_z & 0xff);

    // 距離
    float distance_f = 0.0f;

    // 角度（度数法）の初期値。 0 ～ 359。 石が置けなかったとき、この角度は変更されていく
    int starting_angle_degrees = 0;

    // ##########
    // # ２手目、かつ相手が初手に天元を打ったケース
    // ##########
    //
    //      ３３の星に置きたいので、距離は 6.5 * √2、角度 ３１５°とする。
    //      距離が 6 * √2 では足りなかった。 7 * √2 だとY方向にずれた。
    //
    if (dll_tesuu == 1 && last_z == get_z(9, 9)) {
        // 距離
        distance_f = 6.5f * (float)sqrt(2);

        // 角度
        starting_angle_degrees = 315;
        PRT(L"[%4d手目]  distance_f:%2.2f  starting_angle_degrees:%3d  相手は初手を天元に打った\n", dll_tesuu + 1, distance_f, starting_angle_degrees);
    }
    else {
        int my_last_z;
        int my_last_x;
        int my_last_y;

        int diff_x;
        int diff_y;


        // ##########
        // # ２手目
        // ##########
        //
        //      つまり自分（コンピュータ）が２手目を打つケース
        //
        if (dll_tesuu == 1) {
            // ２手前の自分の手はない。
            // しかし、２手前の自分の手が欲しいので、仮に、２手前の自分は天元に打ったものとしておく
            my_last_z = get_z(9, 9);
            my_last_x = get_x(my_last_z);
            my_last_y = get_y(my_last_z);
        }
        // ##########
        // # 以下、３手目以降
        // ##########
        //
        //      つまり、２手前の手が存在するケース
        // 
        else {

            // ２手前の自分の手
            my_last_z = dll_kifu[dll_tesuu - 2][0];
            my_last_x = get_x(my_last_z);
            my_last_y = get_y(my_last_z);
            //PRT(L"[%4d手目]  my_last_masu:(%2d, %2d)  my_last_z:%04x\n", dll_tesuu + 1, my_last_x + 1, my_last_y + 1, my_last_z & 0xff);
        }

        // 相手の着手と、２手前の自分の着手の距離を測る
        diff_x = last_x - my_last_x;
        diff_y = last_y - my_last_y;

        // ２点の石の距離
        //      ---->   直角三角形の斜辺の長さ
        // 
        //              それだと距離が遠すぎるので、さらに半分にする
        //                  ----> この　÷２　がキツすぎるのでは？
        //                        半分ずつ近づいてきて、シチョウを取りに行く動きをする効果が出る。
        // 
        //              さらに切り捨てで交点１つ分短くなっているように見えるので 0.5 足す
        //                  ----> 0.5 を足して切り捨てれば、四捨五入と同じになるはず
        //
        distance_f = (float)hypot(diff_x, diff_y) / 2.0f + 0.5f;

        // ２点から角度を求める
        float radians = (float)atan((float)diff_y / (float)diff_x);
        starting_angle_degrees = radians_to_degrees(radians) % G_ANGLE_DEGREES_360_SIZE;
    }

    // 着手。未設定
    int ret_z = -1;

    // いくつかの変数は、あとでデバッグ表示します
    int i_constraints;
    float next_distance_f = -1.0f;
    int next_degrees = -1;

    // ##########
    // # （優先順：最後）石を置けなかったら、制限を緩めていく
    // ##########
    //
    //      最初は、自分への指し方のルールを強めに課していて、
    //      そのルールでは指せないこともあるから、そのときは自分に課した指し方のルールを緩めていく
    //
    //      カウントダウンしていく。 0 になったら制約なし、-1 になったら、制約なしでも石を置けなかった
    //
    for (i_constraints = 1; -1 < i_constraints; i_constraints--) {

        // ##########
        // # 石を置けなかったら、距離を変えて置く
        // ##########
        //
        for (int i_offset_distance = 0; i_offset_distance < G_OFFSET_DISTANCE_55_SIZE; i_offset_distance++) {
            int offset_distance = offset_distance_55[i_offset_distance];
            next_distance_f = distance_f + (float)offset_distance;

            // 距離０では、１つ前の手の石の上になるから、無視する
            if (-1 < next_distance_f && next_distance_f < 1) {
                continue;
            }

            // ##########
            // # （優先順：最初）石を置けなかったら、角度を変えて置く
            // ##########
            //
            for (int i_angle = 0; i_angle < G_ANGLE_DEGREES_360_SIZE; i_angle++) {
                int offset_angle_degrees = next_angle_degrees();

                next_degrees = starting_angle_degrees + offset_angle_degrees;

                int offset_y = (int)(next_distance_f * sin(degrees_to_radians(next_degrees)));
                int offset_x = (int)(next_distance_f * cos(degrees_to_radians(next_degrees)));

                // 調整後の座標
                int next_y = offset_y + last_y;
                int next_x = offset_x + last_x;

                // 盤外だ
                if ((next_y < 0 || 19 <= next_y) ||
                    (next_x < 0 || 19 <= next_x)) {

                    // TODO 反射って、要らないのでは？
                    
                    // 制約Ｌｖ１：　盤外に石が飛び出してはいけない
                    // ============================================
                    if (1 <= i_constraints) {
                        continue;
                    }

                    //// 調整前の座標を一時記憶。あとでデバッグ表示で使う
                    //int next_y_before_conditioning = next_y;
                    //int next_x_before_conditioning = next_x;

                    // 盤外に石が飛び出したら、盤外に壁があると思って反射すればいい
                    //
                    //      ---->   盤外で反射するのは、観戦するには分かりづらいから、
                    //              盤外で反射するのは、自分ルールの中での優先順位を下の方にしたい
                    //

                    // 盤外で反射
                    // ==========
                    // 
                    //      盤外に石を投げてしまったら、反射したい
                    //
                    next_y = reflection_y_on_the_wall(next_y);
                    next_x = reflection_x_on_the_wall(next_x);
                }


                // 指し手の試行
                int temp_ret_z = get_z(next_x, next_y);
                destination_color = board[temp_ret_z];

                // 空点には置ける
                if (destination_color == 0) {

                    // 自殺手ならやり直し
                    count_liberty(temp_ret_z);
                    if (g_liberty == 0) {
                        //PRT(L"[%4d手目]  temp_ret_z:%04x  board[temp_ret_z]:%d  自殺手\n", dll_tesuu + 1, temp_ret_z & 0xff, destination_color);
                        //PRT(L"            next_distance_f:%2.2f  =  (  distance_f:%2.2f  +  offset_distance:%2d)\n", next_distance_f, distance_f, offset_distance);
                        //PRT(L"            next_degrees:%3d  =  starting_angle_degrees:%3d  +  offset_angle_degrees:%3d  ...  g_angle_cursor:%3d\n", next_degrees, starting_angle_degrees, offset_angle_degrees, g_angle_cursor);
                        //PRT(L"            next_y_before_conditioning:%2d  =  offset_y:%2d  +  last_y:%2d  ...  next_y:%2d\n", next_y_before_conditioning, offset_y, last_y, next_y);
                        //PRT(L"            next_x_before_conditioning:%2d  =  offset_x:%2d  +  last_x:%2d  ...  next_x:%2d\n", next_x_before_conditioning, offset_x, last_x, next_x);
                        continue;
                    }

                    // コウならやり直し
                    if (is_ko(temp_ret_z)) {
                        //PRT(L"[%4d手目]  temp_ret_z:%04x  board[temp_ret_z]:%d  コウ\n", dll_tesuu + 1, temp_ret_z & 0xff, destination_color);
                        //PRT(L"            next_distance_f:%2.2f  =  (  distance_f:%2.2f  +  offset_distance:%2d)\n", next_distance_f, distance_f, offset_distance);
                        //PRT(L"            next_degrees:%3d  =  starting_angle_degrees:%3d  +  offset_angle_degrees:%3d  ...  g_angle_cursor:%3d\n", next_degrees, starting_angle_degrees, offset_angle_degrees, g_angle_cursor);
                        //PRT(L"            next_y_before_conditioning:%2d  =  offset_y:%2d  +  last_y:%2d  ...  next_y:%2d\n", next_y_before_conditioning, offset_y, last_y, next_y);
                        //PRT(L"            next_x_before_conditioning:%2d  =  offset_x:%2d  +  last_x:%2d  ...  next_x:%2d\n", next_x_before_conditioning, offset_x, last_x, next_x);
                        continue;
                    }

                    // 指し手の更新
                    ret_z = temp_ret_z;

                    //PRT(L"[%4d手目]  ret_z:%04x  board[ret_z]:%d  Ok\n", dll_tesuu + 1, ret_z & 0xff, destination_color);
                    //PRT(L"            next_distance_f:%2.2f  =  (  distance_f:%2.2f  +  offset_distance:%2d)\n", next_distance_f, distance_f, offset_distance);
                    //PRT(L"            next_degrees:%3d  =  starting_angle_degrees:%3d  +  offset_angle_degrees:%3d  ...  g_angle_cursor:%3d\n", next_degrees, starting_angle_degrees, offset_angle_degrees, g_angle_cursor);
                    //PRT(L"            next_y_before_conditioning:%2d  =  offset_y:%2d  +  last_y:%2d  ...  next_y:%2d\n", next_y_before_conditioning, offset_y, last_y, next_y);
                    //PRT(L"            next_x_before_conditioning:%2d  =  offset_x:%2d  +  last_x:%2d  ...  next_x:%2d\n", next_x_before_conditioning, offset_x, last_x, next_x);
                    goto end_of_loop_for_stone_puts;
                }

                //PRT(L"[%4d手目]  ret_z:%04x  board[ret_z]:%d  石がある\n", dll_tesuu + 1, ret_z & 0xff, destination_color);
                //PRT(L"            next_distance_f:%2.2f  =  (  distance_f:%2.2f  +  offset_distance:%2d)\n", next_distance_f, distance_f, offset_distance);
                //PRT(L"            next_degrees:%3d  =  starting_angle_degrees:%3d  +  offset_angle_degrees:%3d  ...  g_angle_cursor:%3d\n", next_degrees, starting_angle_degrees, offset_angle_degrees, g_angle_cursor);
                //PRT(L"            next_y_before_conditioning:%2d  =  offset_y:%2d  +  last_y:%2d  ...  next_y:%2d\n", next_y_before_conditioning, offset_y, last_y, next_y);
                //PRT(L"            next_x_before_conditioning:%2d  =  offset_x:%2d  +  last_x:%2d  ...  next_x:%2d\n", next_x_before_conditioning, offset_x, last_x, next_x);
            }
        }
    }

end_of_loop_for_stone_puts:
    ;



    // 石を置けなかった ----> パスする
    if (ret_z == -1) {
        PRT(L"[%4d手目]  どこにでも石を置こうとしても、石を置けなかったからパスする\n", dll_tesuu + 1);
        return 0;
    }

    // z は表示しても、人間が見ても分からないから省略
    //      "%04x", ret_z
    //
    PRT(L"[%3d手目]  着手:(%2d,%2d)  手番:%d  自分ルールのＬｖ:%2d  距離:%2.2f  角度:%3d\n", dll_tesuu + 1, (ret_z & 0xff), (ret_z >> 8), dll_black_turn, i_constraints, next_distance_f, next_degrees);
    //	print_board();
    return ret_z;
}


// 現在の盤面を表示
void print_board(void)
{
    int x, y, z;
    wchar_t* str[4] = { L"・", L"●", L"○", L"＋" };

    for (y = 0; y < board_size + 2; y++) for (x = 0; x < board_size + 2; x++) {
        z = (y + 0) * 256 + (x + 0);
        PRT(L"%s", str[board[z]]);
        if (x == board_size + 1) PRT(L"\n");
    }
}


// 終局処理（サンプルでは簡単な判断で死石と地の判定をしています）
int endgame_status(int endgame_board[])
{
    int x, y, z, sum, i, k;
    int* p;

    for (y = 0; y < board_size; y++) for (x = 0; x < board_size; x++) {
        z = get_z(x, y);
        p = endgame_board + z;
        if (board[z] == 0) {
            *p = GTP_DAME;
            sum = 0;
            for (i = 0; i < 4; i++) {
                k = board[z + dir4[i]];
                if (k == WAKU) continue;
                sum |= k;
            }
            if (sum == BLACK) *p = GTP_BLACK_TERRITORY;
            if (sum == WHITE) *p = GTP_WHITE_TERRITORY;
        }
        else {
            *p = GTP_ALIVE;
            count_liberty(z);
            //			PRT(L"(%2d,%2d),ishi=%2d,liberty=%2d\n",z&0xff,z>>8, g_ishi, g_liberty);
            if (g_liberty <= 1) *p = GTP_DEAD;
        }
    }
    return 0;
}


// 図形を描く
int endgame_draw_figure(int endgame_board[])
{
    int x, y, z;
    int* p;

    for (y = 0; y < board_size; y++) for (x = 0; x < board_size; x++) {
        z = get_z(x, y);
        p = endgame_board + z;
        if ((rand() % 2) == 0) *p = FIGURE_NONE;
        else {
            if (rand() % 2) *p = FIGURE_BLACK;
            else              *p = FIGURE_WHITE;
            *p |= (rand() % 9) + 1;
        }
    }
    return 0;
}


// 数値を書く(0は表示されない)
int endgame_draw_number(int endgame_board[])
{
    int x, y, z;
    int* p;

    for (y = 0; y < board_size; y++) for (x = 0; x < board_size; x++) {
        z = get_z(x, y);
        p = endgame_board + z;
        *p = (rand() % 110) - 55;
    }
    return 0;
}


// (x,y)を1つの座標に変換
int get_z(int x, int y)
{
    return (y + 1) * 256 + (x + 1);
}


// 位置 tz における呼吸点の数と石の数を計算。結果はグローバル変数に。
void count_liberty(int tz)
{
    // 初期化
    for (int i = 0; i < G_BOARD_AREA_19ROBAN; i++) {
        // 連Id ----> 未指定
        g_ren_id_by_each_node[i] = -1;

        // 連の石の色 ----> 空点
        g_color_each_ren[i] = 0;

        // 連の石の数
        g_stones_each_ren[i] = 0;

        // 連の呼吸点数
        g_liberty_each_ren[i] = 0;
    }


    int i;

    g_last_liberty_z = -1;
    g_liberty = g_ishi = 0;

    for (i = 0; i < BOARD_MAX; i++) {
        check_board[i] = 0;
    }

    count_liberty_sub(tz, board[tz]);
}


// 呼吸点と石の数える再帰関数
// 4方向を調べて、空白だったら+1、自分の石なら再帰で。相手の石、壁ならそのまま。
void count_liberty_sub(int tz, int my_color)
{
    // 新規の連の調査開始


    int z, i;

    check_board[tz] = 1;			    // この石は検索済み	
    g_ishi++;							// 石の数

    for (i = 0; i < 4; i++) {
        z = tz + dir4[i];

        // チェック済みの交点はスキップ
        if (check_board[z]) {
            continue;
        }

        // 空点
        if (board[z] == 0) {
            check_board[z] = 1;	        // この空点は検索済み
            g_last_liberty_z = z;       // 最後に探索した呼吸点
            g_liberty++;				// 呼吸点の数
        }

        // 未探索の自分の石
        if (board[z] == my_color) {
            count_liberty_sub(z, my_color);
        }
    }
}


// 石を消す
void del_stone(int tz, int color)
{
    int z, i;

    board[tz] = 0;
    for (i = 0; i < 4; i++) {
        z = tz + dir4[i];
        if (board[z] == color) del_stone(z, color);
    }
}


// 手を進める。
// z ... 座標、
// color ... 石の色
int move_one(int z, int color)
{
    int i, z1, sum, del_z = 0;
    int all_ishi = 0;	// 取った石の合計
    int un_col = UNCOL(color);

    if (z == 0) {	// PASSの場合
        g_kou_z = 0;
        return MOVE_SUCCESS;
    }
    if (z == g_kou_z) {
        PRT(L"move() Err: コウ！z=%04x\n", z);
        return MOVE_KOU;
    }
    if (board[z] != 0) {
        PRT(L"move() Err: 空点ではない！z=%04x\n", z);
        return MOVE_EXIST;
    }
    board[z] = color;	// とりあえず置いてみる

    for (i = 0; i < 4; i++) {
        z1 = z + dir4[i];
        if (board[z1] != un_col) continue;
        // 敵の石が取れるか？
        count_liberty(z1);
        if (g_liberty == 0) {
            hama[color - 1] += g_ishi;
            all_ishi += g_ishi;
            del_z = z1;	// 取られた石の座標。コウの判定で使う。
            del_stone(z1, un_col);
        }
    }

    // 自殺手を判定
    count_liberty(z);
    if (g_liberty == 0) {
        PRT(L"move() Err: 自殺手! z=%04x\n", z);
        board[z] = 0;
        return MOVE_SUICIDE;
    }

    // 次にコウになる位置を判定。石を1つだけ取った場合。
    g_kou_z = 0;	// コウではない
    if (all_ishi == 1) {
        // 取られた石の4方向に自分のダメ1の連が1つだけある場合、その位置はコウ。
        g_kou_z = del_z;	// 取り合えず取られた石の場所をコウの位置とする
        sum = 0;
        for (i = 0; i < 4; i++) {
            z1 = del_z + dir4[i];
            if (board[z1] != color) continue;
            count_liberty(z1);
            if (g_liberty == 1 && g_ishi == 1) sum++;
        }
        if (sum >= 2) {
            PRT(L"１つ取られて、コウの位置へ打つと、１つの石を2つ以上取れる？z=%04x\n", z);
            return MOVE_FATAL;
        }
        if (sum == 0) g_kou_z = 0;	// コウにはならない。
    }
    return MOVE_SUCCESS;
}
