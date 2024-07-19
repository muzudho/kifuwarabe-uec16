// 第１６回ＵＥＣ杯コンピュータ囲碁大会用きふわらべ
// 
// ベース：
//      CgfGoban.exe用の思考ルーチンのサンプル
//      2005/06/04 - 2005/07/15 山下 宏
//      乱数で手を返すだけです。
//
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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

int ishi = 0;	    // 取った石の数(再帰関数で使う)
int g_liberty = 0;	// 連の呼吸点の数(再帰関数で使う)
int kou_z = 0;	// 次にコウになる位置
int hama[2];	// [0]... 黒が取った石の数, [1]...白が取った石の数
int sg_time[2];	// 累計思考時間

// 角度に乱数を入れていないと、同じパターンでハメれるので、乱数を入れておく
int g_angle_degrees_360[360];
int g_angle_cursor;

// 19 に √2 を掛けるとおよそ 27。 上にも下にも -27 ～ 27 の数を、振動させながら用意する
int offset_distance_55[55];

#define UNCOL(x) (3-(x))	// 石の色を反転させる

// move()関数で手を進めた時の結果
enum MoveResult {
    MOVE_SUCCESS,	// 成功
    MOVE_SUICIDE,	// 自殺手
    MOVE_KOU,		// コウ
    MOVE_EXIST,		// 既に石が存在
    MOVE_FATAL		// それ以外
};



// 関数のプロトタイプ宣言
void count_liberty(int tz);				    // 呼吸点と石の数を調べる
void count_liberty_sub(int tz, int col);	// 呼吸点と石の数を調べる再帰関数
int move_one(int z, int col);			// 1手進める。z ... 座標、col ... 石の色
void print_board(void);					// 現在の盤面を表示
int get_z(int x, int y);					// (x,y)を1つの座標に変換
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
// 対局開始時に一度だけ呼ばれます。
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

    // ##########
    // # 角度
    // ##########

    for (int i = 0; i < 360; i++) {
        g_angle_degrees_360[i] = i;
    }

    // 2πe を掛ければ、だいたい混ざる
    int size = 360;
    int shuffle_number = size * 2 * 3.14159 * 2.71828;
    for (int k = 0; k < shuffle_number; k++) {
        for (int i = 0; i < size; i++) {
            int j = rand() % size;

            // swap
            int temp = g_angle_degrees_360[j];
            g_angle_degrees_360[j] = g_angle_degrees_360[i];
            g_angle_degrees_360[i] = temp;
        }
    }

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
        PRT(L"（x:%2d y:%2d）　石の上には置けない\n", get_x(ret_z), get_y(ret_z));
        return 0;
    }

    // 石の上に置くわけでなければＯＫ。ただし...
    count_liberty(ret_z);

    if (g_liberty == 0) {
        // 呼吸できないところには置けない
        PRT(L"（x:%2d y:%2d）　呼吸できないところには置けない\n", get_x(ret_z), get_y(ret_z));
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
    return (int)(radians * (180.0f / 3.14159f));
}


float degrees_to_radians(int degrees) {
    return (float)degrees * 3.14159f / 180.0f;
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
    kou_z = 0;                      // コウのマス番号を無しにする
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
        int z = dll_kifu[i][0];	    // 座標、y*256 + x の形で入っている
        int col = dll_kifu[i][1];	// 石の色
        int t = dll_kifu[i][2];	    // 消費時間
        sg_time[i & 1] += t;
        if (move_one(z, col) != MOVE_SUCCESS) break;
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


// コウ判定 ----> 簡易の物
// 
// FIXME コウでないケースも、コウと判断することがある。
// 大石を取られた２手後に、大石の一部に打ち込む手とか。
// 正確にやるなら、元々書いてあった山下さんのソースを取り入れること
//
// Returns
// -------
// is_ko
//      0: False
//      1: True
int maybe_it_is_ko(
    int dll_kifu[][3],		// 棋譜
                            // [n][]...手数
                            // [][0]...座標
                            // [][1]...石の色
                            // [][2]...消費時間（秒)
    int dll_tesuu,			// 手数
    int ret_z               // 着手予定座標
)
{
    // １～２手目にコウになることはない
    if (dll_tesuu < 2) {
        return 0;
    }

    // ２手前、つまり自分の１つ前と同じ交点に着手しようとしたら、（コウでないケースもあるが）コウとする
    return dll_kifu[dll_tesuu - 2][0] == ret_z;
}


// 角度を取得
int next_angle_degrees() {
    g_angle_cursor = (g_angle_cursor + 1) % 360;
    return g_angle_degrees_360[g_angle_cursor];
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
    int z, col, t, i, ret_z;

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

    PRT(L"思考時間：先手=%d秒、後手=%d秒\n", sg_time[0], sg_time[1]);

    // ##########
    // # １手目
    // ##########
    //
    //      つまり自分（コンピュータ）が初手を打つケース
    //
    if (dll_tesuu == 0) {
        // 天元に打つ
        int next_x = 9;
        int next_y = 9;
        ret_z = get_z(next_x, next_y);
        PRT(L"[%4d手目]  next(x, y):(%2d, %2d)  ret_z:[%4d %04x]  天元に打つ\n", dll_tesuu + 1, next_x, next_y, ret_z, ret_z & 0xff);
        return ret_z;
    }

    // 以下、２手目以降

    // １手前の相手の手
    int last_z = dll_kifu[dll_tesuu - 1][0];

    // 相手がＰａｓｓなら自分もＰａｓｓ
    if (last_z == 0) {
        ret_z = 0;
        PRT(L"[%4d手目]  pass  ret_z:[%4d, %04x]  相手がパスしたから自分もパス\n", dll_tesuu + 1, ret_z, ret_z & 0xff);
        return ret_z;
    }

    int last_x = get_x(last_z);
    int last_y = get_y(last_z);
    PRT(L"[%4d手目]  last(x, y):(%2d, %2d)  last_z:%04x\n", dll_tesuu + 1, last_x, last_y, last_z & 0xff);

    // 距離 ----> 剰余を使いたいので、整数にします
    float distance_f = 0;

    // 角度（度数法）の初期値。 0 ～ 359。 石が置けなかったとき、この角度は変更されていく
    int starting_angle_degrees = 0;

    // ##########
    // # ２手目、かつ相手が初手に天元を打ったケース
    // ##########
    //
    //      距離は 6 * √2、角度 ４５°とする。
    //
    if (dll_tesuu == 1 && last_z == get_z(9, 9)) {
        // 距離
        distance_f = 6 * 1.4142;

        // 角度
        starting_angle_degrees = 45;
        PRT(L"[%4d手目]  distance_f:%2.2f  starting_angle_degrees:%3d  相手は初手を天元に打った\n", dll_tesuu + 1, distance_f, starting_angle_degrees);
    }
    else {
        PRT(L"[%4d手目]  相手は初手を天元以外に打った\n", dll_tesuu + 1);

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
            PRT(L"[%4d手目]  my_last(x, y):(%2d, %2d)  my_last_z:%04x\n", dll_tesuu + 1, my_last_x, my_last_y, my_last_z & 0xff);
        }

        // 相手の着手と、２手前の自分の着手の距離を測る
        diff_x = last_x - my_last_x;
        diff_y = last_y - my_last_y;

        // ２点の石の距離 ----> 直角三角形の斜辺の長さ
        // それだと距離が遠すぎるので、さらに半分にする
        distance_f = hypot(diff_x, diff_y) / 2.0f;

        // ２点から角度を求める
        float radians = atan((float)diff_y / (float)diff_x);
        starting_angle_degrees = radians_to_degrees(radians) % 360;
    }


    // 石を置けなかったら、角度を変えて置く。それでも置けなかったら、距離を変えて置く
    int i_offset_distance = 0;

    for (; i_offset_distance < 55; i_offset_distance++) {
        int offset_distance = offset_distance_55[i_offset_distance];
        float next_distance_f = distance_f + (float)offset_distance;

        // 距離０では、１つ前の手の石の上になるから、無視する
        if (-1 < next_distance_f && next_distance_f < 1) {
            continue;
        }

        for (int i_angle = 0; i_angle < 360; i_angle++) {
            int offset_angle_degrees = next_angle_degrees();
            int next_degrees = starting_angle_degrees + offset_angle_degrees;

            // TODO radians は整数型（0, 1）にした方が面白い？
            int offset_y = (int)(next_distance_f * sin((int)degrees_to_radians(next_degrees)));
            int offset_x = (int)(next_distance_f * cos((int)degrees_to_radians(next_degrees)));

            int next_y_before_conditioning = offset_y + last_y;
            int next_x_before_conditioning = offset_x + last_x;

            // 盤外に石を投げてしまったら、反射したい
            int next_y = reflection_y_on_the_wall(next_y_before_conditioning);
            int next_x = reflection_x_on_the_wall(next_x_before_conditioning);

            ret_z = get_z(next_x, next_y);
            destination_color = board[ret_z];

            // 空点には置ける
            if (destination_color == 0) {

                // 自殺手ならやり直し
                count_liberty(ret_z);
                if (g_liberty == 0) {
                    PRT(L"[%4d手目]  ret_z:%04x  board[ret_z]:%d  自殺手\n", dll_tesuu + 1, ret_z & 0xff, destination_color);
                    PRT(L"            next_distance_f:%2.2f  =  (  distance_f:%2.2f  +  offset_distance:%2d)\n", next_distance_f, distance_f, offset_distance);
                    PRT(L"            next_degrees:%3d  =  starting_angle_degrees:%3d  +  offset_angle_degrees:%3d\n", next_degrees, starting_angle_degrees, offset_angle_degrees);
                    PRT(L"            next_y_before_conditioning:%2d  =  offset_y:%2d  +  last_y:%2d  ...  next_y:%2d\n", next_y_before_conditioning, offset_y, last_y, next_y);
                    PRT(L"            next_x_before_conditioning:%2d  =  offset_x:%2d  +  last_x:%2d  ...  next_x:%2d\n", next_x_before_conditioning, offset_x, last_x, next_x);
                    continue;
                }

                // 多分、コウならやり直し
                if (maybe_it_is_ko(dll_kifu, dll_tesuu, ret_z)) {
                    PRT(L"[%4d手目]  ret_z:%04x  board[ret_z]:%d  コウ\n", dll_tesuu + 1, ret_z & 0xff, destination_color);
                    PRT(L"            next_distance_f:%2.2f  =  (  distance_f:%2.2f  +  offset_distance:%2d)\n", next_distance_f, distance_f, offset_distance);
                    PRT(L"            next_degrees:%3d  =  starting_angle_degrees:%3d  +  offset_angle_degrees:%3d\n", next_degrees, starting_angle_degrees, offset_angle_degrees);
                    PRT(L"            next_y_before_conditioning:%2d  =  offset_y:%2d  +  last_y:%2d  ...  next_y:%2d\n", next_y_before_conditioning, offset_y, last_y, next_y);
                    PRT(L"            next_x_before_conditioning:%2d  =  offset_x:%2d  +  last_x:%2d  ...  next_x:%2d\n", next_x_before_conditioning, offset_x, last_x, next_x);
                    continue;
                }

                PRT(L"[%4d手目]  ret_z:%04x  board[ret_z]:%d  Ok\n", dll_tesuu + 1, ret_z & 0xff, destination_color);
                PRT(L"            next_distance_f:%2.2f  =  (  distance_f:%2.2f  +  offset_distance:%2d)\n", next_distance_f, distance_f, offset_distance);
                PRT(L"            next_degrees:%3d  =  starting_angle_degrees:%3d  +  offset_angle_degrees:%3d\n", next_degrees, starting_angle_degrees, offset_angle_degrees);
                PRT(L"            next_y_before_conditioning:%2d  =  offset_y:%2d  +  last_y:%2d  ...  next_y:%2d\n", next_y_before_conditioning, offset_y, last_y, next_y);
                PRT(L"            next_x_before_conditioning:%2d  =  offset_x:%2d  +  last_x:%2d  ...  next_x:%2d\n", next_x_before_conditioning, offset_x, last_x, next_x);
                goto end_of_loop_for_distance;
            }

            PRT(L"[%4d手目]  ret_z:%04x  board[ret_z]:%d  石がある\n", dll_tesuu + 1, ret_z & 0xff, destination_color);
            PRT(L"            next_distance_f:%2.2f  =  (  distance_f:%2.2f  +  offset_distance:%2d)\n", next_distance_f, distance_f, offset_distance);
            PRT(L"            next_degrees:%3d  =  starting_angle_degrees:%3d  +  offset_angle_degrees:%3d\n", next_degrees, starting_angle_degrees, offset_angle_degrees);
            PRT(L"            next_y_before_conditioning:%2d  =  offset_y:%2d  +  last_y:%2d  ...  next_y:%2d\n", next_y_before_conditioning, offset_y, last_y, next_y);
            PRT(L"            next_x_before_conditioning:%2d  =  offset_x:%2d  +  last_x:%2d  ...  next_x:%2d\n", next_x_before_conditioning, offset_x, last_x, next_x);
        }
    }
end_of_loop_for_distance:
    ;

    // 置けなかったんだ ----> パスする
    if (55 <= i_offset_distance) {
        PRT(L"[%4d手目]  距離を変えても石を置けなかったからパスする\n", dll_tesuu + 1);
        return 0;
    }

    PRT(L"着手=(%2d,%2d)(%04x), 手数=%d,手番=%d,盤size=%d,komi=%.1f\n", (ret_z & 0xff), (ret_z >> 8), ret_z, dll_tesuu, dll_black_turn, dll_board_size, dll_komi);
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
            //			PRT(L"(%2d,%2d),ishi=%2d,liberty=%2d\n",z&0xff,z>>8,ishi,g_liberty);
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
    int i;

    g_liberty = ishi = 0;
    for (i = 0; i < BOARD_MAX; i++) check_board[i] = 0;
    count_liberty_sub(tz, board[tz]);
}


// 呼吸点と石の数える再帰関数
// 4方向を調べて、空白だったら+1、自分の石なら再帰で。相手の石、壁ならそのまま。
void count_liberty_sub(int tz, int col)
{
    int z, i;

    check_board[tz] = 1;			// この石は検索済み	
    ishi++;							// 石の数
    for (i = 0; i < 4; i++) {
        z = tz + dir4[i];
        if (check_board[z]) continue;
        if (board[z] == 0) {
            check_board[z] = 1;	        // この空点は検索済み
            g_liberty++;				// 呼吸点の数
        }
        if (board[z] == col) count_liberty_sub(z, col);	// 未探索の自分の石
    }
}


// 石を消す
void del_stone(int tz, int col)
{
    int z, i;

    board[tz] = 0;
    for (i = 0; i < 4; i++) {
        z = tz + dir4[i];
        if (board[z] == col) del_stone(z, col);
    }
}


// 手を進める。
// z ... 座標、
// col ... 石の色
int move_one(int z, int col)
{
    int i, z1, sum, del_z = 0;
    int all_ishi = 0;	// 取った石の合計
    int un_col = UNCOL(col);

    if (z == 0) {	// PASSの場合
        kou_z = 0;
        return MOVE_SUCCESS;
    }
    if (z == kou_z) {
        PRT(L"move() Err: コウ！z=%04x\n", z);
        return MOVE_KOU;
    }
    if (board[z] != 0) {
        PRT(L"move() Err: 空点ではない！z=%04x\n", z);
        return MOVE_EXIST;
    }
    board[z] = col;	// とりあえず置いてみる

    for (i = 0; i < 4; i++) {
        z1 = z + dir4[i];
        if (board[z1] != un_col) continue;
        // 敵の石が取れるか？
        count_liberty(z1);
        if (g_liberty == 0) {
            hama[col - 1] += ishi;
            all_ishi += ishi;
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
    kou_z = 0;	// コウではない
    if (all_ishi == 1) {
        // 取られた石の4方向に自分のダメ1の連が1つだけある場合、その位置はコウ。
        kou_z = del_z;	// 取り合えず取られた石の場所をコウの位置とする
        sum = 0;
        for (i = 0; i < 4; i++) {
            z1 = del_z + dir4[i];
            if (board[z1] != col) continue;
            count_liberty(z1);
            if (g_liberty == 1 && ishi == 1) sum++;
        }
        if (sum >= 2) {
            PRT(L"１つ取られて、コウの位置へ打つと、１つの石を2つ以上取れる？z=%04x\n", z);
            return MOVE_FATAL;
        }
        if (sum == 0) kou_z = 0;	// コウにはならない。
    }
    return MOVE_SUCCESS;
}
