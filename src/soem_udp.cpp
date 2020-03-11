#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "mbed.h"
#include "EthernetInterface.h"
#include <SOEM.h>

// デバッグ出力用
Serial pc(USBTX, USBRX);

// UDP通信用
Mutex g_mutex;
uint8_t  g_servo[4];    // サーボ目標角度     (MANGO→PC)
uint16_t g_volume[4];   // ボリューム入力値   (MANGO→PC)
int16_t  g_gamepad[4];  // ゲームパッド入力値 (MANGO←PC)
bool g_remoteOn = false; // 遠隔操作ONフラグ
int g_init_cnt = 0;      // 初期動作カウンタ
bool g_init_flag = true; // 初期動作フラグ

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// IOデータバッファ
static char IOmap[4096];
// 期待されるWKC値
static int expectedWKC;

// 開く
// nif: ネットワークインターフェースID
// return: 0=失敗 / 1=成功
int soem_open(char* nif)
{
	int ret = ec_init(nif);
	return ret;
}

// 閉じる
void soem_close(void)
{
	ec_close();
}

#define ALL_SLAVES_OP_STATE	0	// 全てのスレーブがOP状態になりました (成功)
#define NO_SLAVES_FOUND		1	// スレーブがみつかりません
#define NOT_ALL_OP_STATE	2	// OP状態にならないスレーブがあります

// コンフィグする
// return 結果
int soem_config(void)
{
	int oloop, iloop, chk;
	
	// スレーブを見つけ、自動コンフィグする
	if ( ec_config_init(FALSE) > 0 )
	{
		printf("%d slaves found and configured.\n",ec_slavecount);
		
		ec_config_map(&IOmap);
		ec_configdc();
		
		printf("Slaves mapped, state to SAFE_OP.\n");
		
		// 全てのスレーブが SAFE_OP 状態に達するのを待つ
		ec_statecheck(0, EC_STATE_SAFE_OP,	EC_TIMEOUTSTATE * 4);
		oloop = ec_slave[0].Obytes;
		if ((oloop == 0) && (ec_slave[0].Obits > 0)) oloop = 1;
		if (oloop > 8) oloop = 8;
		iloop = ec_slave[0].Ibytes;
		if ((iloop == 0) && (ec_slave[0].Ibits > 0)) iloop = 1;
		if (iloop > 8) iloop = 8;
		printf("segments : %d : %ld %ld %ld %ld\n",
			ec_group[0].nsegments,
			ec_group[0].IOsegment[0],
			ec_group[0].IOsegment[1],
			ec_group[0].IOsegment[2],
			ec_group[0].IOsegment[3]);
		printf("Request operational state for all slaves\n");
		expectedWKC = (ec_group[0].outputsWKC * 2) + ec_group[0].inputsWKC;
		printf("Calculated workcounter %d\n", expectedWKC);
		
		// 全てのスレーブにOP状態を要求
		ec_slave[0].state = EC_STATE_OPERATIONAL;
		/* send one valid process data to make outputs in slaves happy*/ // ←意味不明
		ec_send_processdata();
		ec_receive_processdata(EC_TIMEOUTRET);
		ec_writestate(0);
		chk = 40;
		// 全てのスレーブがOP状態に達するのを待つ
		do
		{
			ec_send_processdata();
			ec_receive_processdata(EC_TIMEOUTRET);
			ec_statecheck(0, EC_STATE_OPERATIONAL, 50000);
		}
		while (chk-- && (ec_slave[0].state != EC_STATE_OPERATIONAL));
		
		if (ec_slave[0].state == EC_STATE_OPERATIONAL )
		{
			return ALL_SLAVES_OP_STATE;
		}
		else
		{
			return NOT_ALL_OP_STATE;
		}
	}
	else
	{
		return NO_SLAVES_FOUND;
	}
}

// スレーブの数を取得
// return: スレーブの数
int soem_getSlaveCount(void)
{
	return ec_slavecount;
}

// スレーブの状態を更新
// return: 全スレーブの中で最も低い状態
int soem_updateState(void)
{
	int ret = ec_readstate();
	return ret;
}

// スレーブの状態を取得
// slave: スレーブのインクリメンタルアドレス
// return: 状態
int soem_getState(int slave)
{
	return ec_slave[slave].state;
}

// スレーブのALステータスコードを取得
// slave: スレーブのインクリメンタルアドレス
// return: ALステータスコード
int soem_getALStatusCode(int slave)
{
	return ec_slave[slave].ALstatuscode;
}

// スレーブのALステータスの説明を取得
// slave: スレーブのインクリメンタルアドレス
// desc: ALステータスの説明 (最大31文字)
void soem_getALStatusDesc(int slave, char* desc)
{
	snprintf(desc, 31, "%s", ec_ALstatuscode2string( ec_slave[slave].ALstatuscode ));
}

// スレーブの状態変更を要求
void soem_requestState(int slave, int state)
{
	ec_slave[slave].state = state;
	ec_writestate(slave);
}

// スレーブの名前を取得
// slave: スレーブのインクリメンタルアドレス
// name: スレーブの名前 (最大31文字)
void soem_getName(int slave, char* name)
{
	snprintf(name, 31, "%s", ec_slave[slave].name );
}

// スレーブのベンダ番号/製品番号/バージョン番号を取得
// slave: スレーブのインクリメンタルアドレス
// id: {ベンダ番号, 製品番号, バージョン番号}
void soem_getId(int slave, unsigned long* id)
{
	id[0] = ec_slave[slave].eep_man;
	id[1] = ec_slave[slave].eep_id;
	id[2] = ec_slave[slave].eep_rev;
}

// PDO転送する
// return:  0=失敗 / 1=成功
int soem_transferPDO(void)
{
	ec_send_processdata();
	int wkc = ec_receive_processdata(EC_TIMEOUTRET);

	if(wkc >= expectedWKC){
		return 1;
	}else{
		return 0;
	}
}

// 汎用スレーブPDO入力
// slave: スレーブのインクリメンタルアドレス
// offset: オフセットアドレス
// return: 入力値
uint8_t soem_getInputPDO(int slave, int offset)
{
	uint8_t ret = 0;
	
	if(slave <= ec_slavecount)
	{
		ret = ec_slave[slave].inputs[offset];
	}
	return ret;
}

// 汎用スレーブPDO出力
// slave: スレーブのインクリメンタルアドレス
// offset: オフセットアドレス
// value: 出力値
void soem_setOutPDO(int slave, int offset, uint8_t value)
{
	if(slave <= ec_slavecount)
	{
		ec_slave[slave].outputs[offset] = value;
	}
}

// ロボットアーム制御
void robot_arm_ctrl(void)
{
	char ifname[] = "EthernetShield2"; // It's dummy
	
	int result = soem_open(ifname);
	if(result == 0)
	{
		printf("can not open network adapter!\n");
		return ;
	}
	result = soem_config();
	if(result == NO_SLAVES_FOUND)
	{
		printf("slaves not found!\n");
		return ;
	}
	if(result == NOT_ALL_OP_STATE)
	{
		printf("at least one slave can not reach OP state!\n");
		return ;
	}
	
	// 時間確認用
	//int pin2_output = 0;
	//pinMode(2, OUTPUT);
	//digitalWrite(2, pin2_output);

	// サーボの角度 {肩, 肘, 手首, 指} (0-180deg)
	const int L_DEG[4] = {  0,  55, 180, 100}; // ボリューム=   0のときの角度 {右, 下, 下, 閉}
	const int H_DEG[4] = {180, 175,   0,  35}; // ボリューム=1023のときの角度 {左, 上, 上, 開}
	const int I_DEG[4] = { 90,  55,  90, 100}; // 初期角度 {中, 下, 中, 閉}
	const int S_PAD[4] = {  1,  -1,   1,  -1}; // ゲームパッド極性補正
	uint8_t servo[4];   // サーボの目標角度
	uint16_t volume[4]; // ボリュームの入力値
	float fServo[4]; // サーボの目標角度計算用(ゲームパッド使用時)
	for(int i=0;i<4;i++){
		servo[i] = I_DEG[i];
	}

	g_init_cnt = 0;     // 初期動作カウンタ
    g_init_flag = true; // 初期動作フラグ
	while (1)
	{
		// 時間確認用
		//pin2_output = 1 - pin2_output;
		//digitalWrite(2, pin2_output);

		for(int i=0;i<4;i++){
			soem_setOutPDO(2, i, servo[i]);
		}
		soem_setOutPDO(2, 4, 0xA5);
		
		soem_transferPDO();

		// 初期動作(ゆっくり目標角度へ)
        g_mutex.lock();
		if(g_init_cnt < 180){
			g_init_cnt++;
            if(g_init_cnt >= 180) g_init_flag = false;
			wait_ms(10);
		}
        g_mutex.unlock();
		// 各々のサーボについて
		for(int i=0;i<4;i++){
			// ボリューム値を目標角度へ換算
			uint8_t h = soem_getInputPDO(1, 2*i + 0);
			uint8_t l = soem_getInputPDO(1, 2*i + 1);
			uint16_t vol = ((uint16_t)h << 8) | (uint16_t)l;
			if(vol > 1023) vol = 1023;
			volume[i] = vol;
            int deg;
            g_mutex.lock();
            if(g_remoteOn){
                // deg = map(g_gamepad[i], -1000, 1000, L_DEG[i], H_DEG[i]);
                float maxdeg = (L_DEG[i] > H_DEG[i]) ? L_DEG[i] : H_DEG[i];
                float mindeg = (L_DEG[i] < H_DEG[i]) ? L_DEG[i] : H_DEG[i];
                fServo[i] = fServo[i] + (float)(g_gamepad[i] * S_PAD[i]) / 3000.0f;
                if(fServo[i] < mindeg) fServo[i] = mindeg;
                if(fServo[i] > maxdeg) fServo[i] = maxdeg;
                deg = (int)fServo[i];
            }else{
			    deg = map(vol, 0, 1023, L_DEG[i], H_DEG[i]);
			    fServo[i] = (float)deg;
            }
			// 初期動作(ゆっくり目標角度へ)
			if(g_init_flag){
				if     (servo[i] < deg) servo[i]++;
				else if(servo[i] > deg) servo[i]--;
			}
			// 通常動作
			else{
				servo[i] = deg;
			}
            g_mutex.unlock();
			printf("%d ", servo[i]);
			printf(",");
		}
		printf("\n");
		
		// UDP通信用変数更新
		g_mutex.lock();
		for(int i=0;i<4;i++){
			g_servo[i]  = servo[i];
			g_volume[i] = volume[i];
		}
		g_mutex.unlock();
		
	}// while(true)

	// 切断処理
	soem_requestState(0, EC_STATE_INIT);
	soem_close();
}

// UDP/IP用のネットワークインターフェース
EthernetInterface eth;

// UDP送信スレッド
Thread udpSendThread;
// UDP受信スレッド
Thread udpRecvThread;

// 自ホスト(マイコン)のIPアドレスとポート番号
static const char *IPadrees    = "192.168.10.2";
static const char *NetworkMask = "255.255.255.0";
static const char *Gateway     = "192.168.10.1";
static const uint16_t LocalPort = 4002;

// 相手ホスト(PC)のIPアドレスとポート番号
static const char *RemoteAddress = "192.168.10.1";
static const uint16_t RemotePort = 4001;

// 生存応答コマンド送信リクエストフラグ
static bool reqAliveCommand = false;

// UDP通信用NIC初期化
void udpNicInit()
{
    // ネットワークインターフェース接続
    printf("Initializing NIC for UDP\n");
    eth.set_dhcp(false);
    eth.set_network(IPadrees, NetworkMask, Gateway);
    if(0 != eth.connect()) {
        printf("Error: NIC connecting failed\n");
        while(true);
    }
    printf("NIC connected\n");
    // IPアドレス確認
    const char *ip = eth.get_ip_address();
    printf("IP address is: %s\n", ip ? ip : "No IP");
}


// UDP通信用NIC終了処理
void udpNicClose()
{
    eth.disconnect();
    printf("NIC disconnected\n");
}

// UDP送信スレッド関数
void udpSendFunc()
{
    // 送信用UDPソケット
    UDPSocket sendSock;
    sendSock.open(&eth);

    // メインループ
    while(true){
        // 送信
        static int cnt = 0;
        cnt++;
        if(cnt>=10){
            cnt = 0;
            // Dコマンド送信
            uint8_t send_buf[15];
            send_buf[0] = '#';
            send_buf[1] = 'D';
            g_mutex.lock();
            for(int i=0;i<4;i++){
                send_buf[2 + i*2] = (uint8_t)(g_volume[i] >> 8);
                send_buf[3 + i*2] = (uint8_t)(g_volume[i] & 0xFF);
                send_buf[10 + i] = g_servo[i];
            }
            g_mutex.unlock();
            send_buf[14] = '$';
            //printf("send D\n");
            int sentSize = sendSock.sendto(RemoteAddress, RemotePort, send_buf, sizeof(send_buf));
            if(sentSize < 0) {
                printf("Error sending D Command\n");
            }
        }
        if(reqAliveCommand){
            reqAliveCommand = false;
            // Aコマンド送信
            uint8_t send_buf[3];
            send_buf[0] = '#';
            send_buf[1] = 'A';
            send_buf[2] = '$';
            printf("send A\n");
            int sentSize = sendSock.sendto(RemoteAddress, RemotePort, send_buf, sizeof(send_buf));
            if(sentSize < 0) {
                printf("Error sending A Command\n");
            }
        }
        wait_ms(10);
    }
    // ソケットを閉じる
    sendSock.close();
}

// UDP受信スレッド関数
void udpRecvFunc()
{
    // 受信用UDPソケット
    UDPSocket recvSock;
    recvSock.open(&eth);
    int bind = recvSock.bind(LocalPort);
    printf("UDP receive socket bind return: %d\n", bind);

    while(true)
    {
        // 受信
        uint8_t recv_buf[256];
        SocketAddress remoteHost; // 相手ホストの情報取得用
        int n = recvSock.recvfrom(&remoteHost, &recv_buf, sizeof(recv_buf));
        // printf("PC Address: %s Port: %d\n\r", remoteHost.get_ip_address(), remoteHost.get_port());
        // 受信があったら
        if(n > 0)
        {
            //printf("received %d byte\n", n);
            if(recv_buf[0] == '#')
            {
                switch(recv_buf[1]){
                    case 'A':
                        if(n < 3){
                            printf("A: Bad Size %d\n", n);
                            break;
                        }
                        if(recv_buf[2] == '$'){
                            reqAliveCommand = true;
                            printf("A: Received!\n");
                        }else{
                            printf("A: Bad Tail Code %02X\n", recv_buf[2]);
                        }
                        break;
                    case 'R':
                        if(n < 4){
                            printf("R: Bad Size %d\n", n);
                            break;
                        }
                        if(recv_buf[3] == '$'){
                            uint8_t remoteOn = recv_buf[2];
                            g_mutex.lock();
                            if(remoteOn == 0){
                                g_remoteOn = false;
                                g_init_cnt = 0;
                                g_init_flag = true;
                                //printf("R: OFF!\n");
                            }else if(remoteOn == 1)
                            {
                                g_remoteOn = true;
                                //g_init_cnt = 0;
                                //g_init_flag = true;
                                //printf("R: ON!\n");
                            }else{
                                printf("R: Bad Parameter %02X\n", remoteOn);
                            }
                            g_mutex.unlock();
                        }else{
                            printf("R: Bad Tail Code %02X\n", recv_buf[3]);
                        }
                        break;
                    case 'C':
                        if(n < 11){
                            printf("C: Bad Size %d\n", n);
                            break;
                        }
                        if(recv_buf[10] == '$'){
                            g_mutex.lock();
                            for(int i=0;i<4;i++){
                                g_gamepad[i] = (int16_t)(
                                    ((uint16_t)recv_buf[2 + 2*i] << 8) | (uint16_t)recv_buf[3 + 2*i]
                                );
                            }
                            printf("C: %4d, %4d, %4d, %4d\n", g_gamepad[0], g_gamepad[1], g_gamepad[2], g_gamepad[3]);
                            g_mutex.unlock();
                        }else{
                            printf("C: Bad Tail Code %02X\n", recv_buf[10]);
                        }
                        break;
                    default:
                        printf("Unknown Command %02X\n", recv_buf[1]);
                        break;
                }
            }else{
                printf("Bad Head Code %02X", recv_buf[0]);
            }
        }
    }
    
    // ソケットを閉じる
    recvSock.close();
}

int main()
{
	pc.baud(115200);
    pc.printf("ec_master\n");

    wait_ms(1000);

    pc.printf("ec_master start\n");

    // UDP通信用NIC初期化
    udpNicInit();

    // 受信スレッド開始
    udpRecvThread.start(udpRecvFunc);
    // 送信スレッド開始
    udpSendThread.start(udpSendFunc);

    while(1)
    {
    	robot_arm_ctrl();
        //wait(1);
    }
}
