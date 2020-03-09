﻿using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Threading;

namespace WinMangoCAT
{
    public partial class Form1 : Form
    {
        // バックグラウンド処理
        BackgroundWorker worker;
        // ゲームパッド入力
        GamePad gamePad;
        // UDP通信
        UdpComm udpComm;
        // フラグ
        bool isOpen = false; // LAN接続
        bool remoteOn = false; // コントロール切り替え
        bool isConnected = false; // 接続完了
        object lockObj = new object(); // ロック用オブジェクト
        // データ
        int[] adval = new int[4];
        int[] servo = new int[4];
        // データ表示用
        TextBox[] tb_adval = new TextBox[4];
        TextBox[] tb_servo = new TextBox[4];
        TextBox[] tb_stick = new TextBox[4];

        // 初期化する
        public Form1()
        {
            InitializeComponent();

            // 設定読み出し
            textLocalAddress.Text = Properties.Settings.Default.localAddress;
            textRemoteAddress.Text = Properties.Settings.Default.remoteAddress;

            // 変数の初期化
            for(int i = 0; i < 4; i++)
            {
                adval[i] = 0;
                servo[i] = 0;
            }
            tb_adval[0] = textAD0;
            tb_adval[1] = textAD1;
            tb_adval[2] = textAD2;
            tb_adval[3] = textAD3;
            tb_servo[0] = textServo0;
            tb_servo[1] = textServo1;
            tb_servo[2] = textServo2;
            tb_servo[3] = textServo3;
            tb_stick[0] = textPad0;
            tb_stick[1] = textPad1;
            tb_stick[2] = textPad2;
            tb_stick[3] = textPad3;

            // ゲームパッドの初期化
            gamePad = new GamePad();
            gamePad.Init();
            // UDP通信の初期化
            udpComm = new UdpComm();
            udpComm.onReceive += onReceive;

            // バックグラウンド処理
            worker = new BackgroundWorker();
            worker.WorkerSupportsCancellation = true;
            worker.WorkerReportsProgress = true;
            worker.ProgressChanged += new ProgressChangedEventHandler(worker_ProgressChanged);
            worker.DoWork += new DoWorkEventHandler(worker_DoWork);
            worker.RunWorkerAsync();
        }

        // 終了時処理
        private void Form1_FormClosing(object sender, FormClosingEventArgs e)
        {
            udpComm.Close();

            Properties.Settings.Default.localAddress = textLocalAddress.Text;
            Properties.Settings.Default.remoteAddress = textRemoteAddress.Text;
            Properties.Settings.Default.Save();
        }

        // バックグラウンド処理
        private void worker_DoWork(object sender, DoWorkEventArgs e)
        {
            while (true)
            {
                // ゲームパッド入力
                AnalogInput analog;
                lock (lockObj)
                {
                    analog = gamePad.Get();
                }
                // 表示更新
                worker.ReportProgress(0, analog);

                // UDP通信を開いている場合
                lock (lockObj)
                {
                    if (isOpen)
                    {
                        // 接続確認
                        if (!isConnected)
                        {
                            //Console.WriteLine("sending A");
                            byte[] data = new byte[3];
                            data[0] = (byte)'#';
                            data[1] = (byte)'A';
                            data[2] = (byte)'$';
                            udpComm.Send(data);
                            Console.WriteLine("sent A");
                        }
                        // リモートONならゲームパッド入力値を送信
                        if (remoteOn)
                        {
                            byte[] data = new byte[11];
                            data[0] = (byte)'#';
                            data[1] = (byte)'C';
                            for (int i = 0; i < 4; i++)
                            {
                                data[2 + 2 * i] = (byte)(analog.val[i] >> 8);
                                data[3 + 2 * i] = (byte)(analog.val[i] & 0xFF);
                            }
                            data[10] = (byte)'$';
                            udpComm.Send(data);
                        }
                    }
                }
                Thread.Sleep(20);
            }
        }

        // 表示更新
        // e.UserState: アナログ入力値
        private void worker_ProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            AnalogInput analog = (AnalogInput)e.UserState;

            for(int i = 0; i < 4; i++)
            {
                tb_stick[i].Text = analog.val[i].ToString();
            }
        }

        // 接続
        private void buttonConnect_Click(object sender, EventArgs e)
        {
            lock (lockObj)
            {
                if (isOpen)
                {
                    // 表示更新
                    isOpen = false;
                    buttonConnect.Text = "接続";
                    textLocalAddress.ReadOnly = false;
                    remoteOn = false;
                    labelCtrlState.Text = "リモートOFF";
                    isConnected = false;
                    labelConnectState.Text = "未接続";
                    labelCtrlState.ForeColor = Color.Black;

                    // 閉じる
                    udpComm.Close();
                }
                else
                {
                    isConnected = false;

                    // 開く
                    bool ret = udpComm.Open(textLocalAddress.Text, textRemoteAddress.Text);
                    if (!ret) return;

                    // 表示更新
                    isOpen = true;
                    buttonConnect.Text = "切断";
                    textLocalAddress.ReadOnly = true;
                }
            }
        }

        // コントロール切り替え
        private void buttonControl_Click(object sender, EventArgs e)
        {
            lock (lockObj)
            {
                if (isOpen)
                {
                    if (remoteOn)
                    {
                        remoteOn = false;
                        labelCtrlState.Text = "リモートOFF";
                        labelCtrlState.ForeColor = Color.Black;

                        byte[] data = new byte[4];
                        data[0] = (byte)'#';
                        data[1] = (byte)'R';
                        data[2] = 0;
                        data[3] = (byte)'$';
                        udpComm.Send(data);
                    }
                    else
                    {
                        byte[] data = new byte[4];
                        data[0] = (byte)'#';
                        data[1] = (byte)'R';
                        data[2] = 1;
                        data[3] = (byte)'$';
                        udpComm.Send(data);

                        remoteOn = true;
                        labelCtrlState.Text = "リモートON";
                        labelCtrlState.ForeColor = Color.Red;
                    }
                }
            }
        }

        // ゲームパッド再接続
        private void buttonGamepad_Click(object sender, EventArgs e)
        {
            lock (lockObj)
            {
                gamePad.Init();
            }
        }

        // UDP受信時
        private void onReceive(object sender, UdpEventArgs e)
        {
            byte[] data = e.data;
            
            // 先頭文字チェック
            if(data[0] != (byte)'#')
            {
                return;
            }
            // コマンド別処理
            switch (data[1])
            {
                // A/D値・サーボ指令値コマンド
                case (byte)'D':
                    if (data[14] == (byte)'$')
                    {
                        for(int i = 0; i < 4; i++)
                        {
                            adval[i] = ((int)data[2 + 2 * i] << 8)  | (int)data[3 + 2 * i];
                            servo[i] = (int)data[10 + i];
                        }
                        this.BeginInvoke((Action)(() => {
                            for (int i = 0; i < 4; i++)
                            {
                                tb_adval[i].Text = adval[i].ToString();
                                tb_servo[i].Text = servo[i].ToString();
                            }
                        }));
                    }
                    break;
                // 生存確認コマンド
                case (byte)'A':
                    if(data[2] == (byte)'$')
                    {
                        Console.WriteLine("Received A");
                        isConnected = true;
                        this.BeginInvoke((Action)(() => {
                            labelConnectState.Text = "接続済み";
                        }));
                    }
                    break;
                // 不明なコマンド
                default:
                    break;
            }
        }
    }
}
