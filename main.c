/*
 * 电子信息杯初赛代码 (由于ad转换不能转换负压, 需将单片机GND设为负压)
 * date: 2021/12/09
 * author: vaine
 * describe: 主循环中时钟使用display_THD()函数 在数码管上显示全局变量THD的值;
 *           在计时器中断函数中使用refresh_THD()函数定时刷新THD的值.
 * display_THD(): 数码管显示 float THD (只显示 个位 & 小数部分前三位);
 *                可能由于浮点数精度出现输出!=输入, 但是最多差0.001
 * refresh_THD(): 刷新THD的值.
 *                先读取sample_num个采样值(采样频率sample_num*1kHz), 并格式化为float;
 *                再DFT得到各频率分量; 最后根据频率分量计算THD(开方使用二分法)
 * TIPs:1. 采样尽量快, 因为单片机时钟频率有点低, 采样频率有可能达不到 (可以尝试使用 外部晶振 & 中断采样)
 * PROB:1. 题目要求5次谐波, 但是5次谐波需要采样频率10kHz以上(FFT需要16kHz), 使用内部晶振不好达到
 *      2. 由于ad转换不能转换负压, 需将单片机GND设为负压
 *      3. ref_time过快的话不行(不能比refresh_THD()快)
 * TODO:1. 推测因为采样频率不准导致THD过大. 目前采用硬延迟, 尝试使用中断.
 *      2. 用FFT试试能不能提速
 */

#include <msp430g2553.h>


// 采样点数
#define sample_num 8 // 改变sample_num还需调整sample_gap, Cos[], Sin[]
// 改变sample_num需要手动调整sample_gap, 使采样频率达到sample_num*1kHz
const unsigned int sample_gap = 4; // 时钟测试得到 5 对应采样频率8kHz, 但发现改成 4 THD更准
const unsigned int  ref_time  = 1000 / 100;      // THD刷新时间间隔; n ms / 100 (防止溢出)(尽量大于500ms)
const float ref_vcc = 3.3; // ad参考电压; V

const unsigned char display_pos[3] = {BIT1, BIT2, BIT3};
const unsigned char display_num[10] = {0x03, 0x9F, 0x25, 0x0D, 0x99, 0x49, 0x41, 0x1F, 0x01, 0x09};
const float Cos[sample_num] = {1.000, 0.707, 0.000, -0.707, -1.000, -0.707, 0.000, 0.707};
const float Sin[sample_num] = {0.000, 0.707, 1.000, 0.707, 0.000, -0.707, -1.000, -0.707};

float THD = 0.000; // 全局变量THD, display_THD()始终显示THD; 计算函数refresh_THD()在计时中断进入, 不断更新算得的THD
int sample_points[sample_num];
float sample_nums[sample_num];

/*
 * 刷新THD.
 * 先读取sample_num个采样值
 * 再DFT得到各频率分量
 * 最后根据频率分量计算THD
 */
void refresh_THD(void);
/*
 * 数码管显示 float THD (只显示 个位 & 小数部分前三位)
 * 1 > THD >= 0
 * 可能由于浮点数精度出现输出!=输入, 但是最多差0.001
 */
void display_THD(void);
/*
 * 1ms延时函数
 */
void delay_1ms(void);
/*
 * nms延时函数
 */
void delay_nms(unsigned int k);
/*
 * 引脚初始化函数
 */
void GPIO_Init(void);
/*
 * 定时器中断初始化
 */
void timer0_init();
/*
 * AD转换初始化
 */
void adc_init(void);
/*
 * 采样一次, 并将采样结果存入sample_points[n]
 */
void sampling(int n);
/*
 * 牛顿法求平方根
 */
float my_sqrt(float n);
/*
 * 测试时钟频率, 找到能够达到采样频率的延迟
 */
void clock_test(void);

/*
 * 主函数
 */
int main(void)
{
    WDTCTL = WDTPW | WDTHOLD; // stop watchdog timer
    GPIO_Init();
    adc_init();
    timer0_init();
    _EINT();          //开总中断
    delay_nms(500);
    while(1)
    {
        display_THD();
        // clock_test();
    }
}

void display_THD(void)
{
    float n = THD;
    unsigned int temp = (int)n;
    n -= temp;
    P2OUT = 0xFF; // 输出前全灭
    P3OUT = BIT0; // 第一位
    P2OUT = display_num[temp]; // 显示"x"
    P2OUT &= 0xFE;             // 显示小数点
    delay_nms(10);
    unsigned int i;
    for (i = 0; i < 3; i++)
    {
        P2OUT = 0xFF; // 输出前全灭
        P3OUT = display_pos[i];
        n = n * 10;
        unsigned int temp = (int)n;
        P2OUT = display_num[temp];
        n -= temp;
        delay_nms(10);
    }
}

/*
 * 定时器中断响应函数, 定时刷新THD的值
 */
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer0_A0(void)
{
    refresh_THD();
    // P3OUT ^= BIT4;
}

void refresh_THD(void)
{
    // 数码管显示 '----'
    P2OUT = 0xFF;
    P3OUT = BIT0+BIT1+BIT2+BIT3;
    P2OUT = 0xFD;
    // 8kHz采样8次, 结果存在sample_points[]
    int i = 0;
    for(; i<sample_num; i++)
    {
        sampling(i);
        int j = 0;
        while(j<sample_gap) // j<5
        {
            j++;
        }
    }
    // 将采样点值变为浮点数存在sample_nums[]
    for(i=0; i<sample_num; i++)
    {
        sample_nums[i] = (float)(sample_points[i]) / 1023.0 * ref_vcc;
    }
    // DFT提取频率分量
    float real[5] = {0.00, 0.00, 0.00, 0.00, 0.00};
    float imag[5] = {0.00, 0.00, 0.00, 0.00, 0.00};
    int k = 0;
    int n = 0;
    for(k=1; k<5; k++)
    {
        for(n=0; n<sample_num; n++)
        {
            int idx = (n*k) % sample_num; // (n*k) mod sample_num
            real[k] += sample_nums[n]*Cos[idx];
            imag[k] += sample_nums[n]*Sin[idx];
        }
    }
    // 计算THD
    THD =  real[2]*real[2] + real[3]*real[3] + real[4]*real[4];
    THD += imag[2]*imag[2] + imag[3]*imag[3] + imag[4]*imag[4];
    THD /= real[1]*real[1] + imag[1]*imag[1];
    // 开根号
    // THD = 0.5;
    THD = my_sqrt(THD);
    // 数码管结束显示 '----'
    P2OUT = 0xFF;
    P3OUT = 0x00;
}

float my_sqrt(float n)
{
    if (n <= 0.0)
    {
        return 0.0;
    }
    const float _JINGDU = 1e-4;
    float _avg = n;
    float last_avg = n;
    _avg = (_avg + n / _avg) / 2;
    float temp = _avg - last_avg;
	if (temp<0) temp = -temp;
    while ( temp > _JINGDU)
    {
        last_avg = _avg;
        _avg = (_avg + n / _avg) / 2;
        temp = _avg - last_avg;
        if (temp<0) temp = -temp;
    }
    return _avg;

    /*
    * **方法太慢, 弃用**
    * 用于开根求THD, THD一般都小于1
    * 1 >= n >= 0
    * 需要计算更大的开方就增大temp(0x0080<<a)和temp2(0x0040<<2a)
    * 本质上是二分法, 从高位到低位 判断二进制数的每一位为1或0
    */
    // float temp  = 0.5;  // 判断小数位第一位是不是1
    // float temp2 = 0.25; // temp的平方
    // float result  = 0.0;// 结果
    // float result2 = 0.0;// result的平方
    // while(temp2 != 0.0)
    // {
    //     float temp_result2 = result2 + 2*result*temp + temp2;
    //     if (temp_result2 <= n)
    //     {
    //         result += temp;
    //         result2 = temp_result2;
    //     }
    //     temp  = temp  / 2; // 判断小数位下一位是不是1
    //     temp2 = temp2 / 4;
    // }
    // return result;
}

void clock_test(void) // TODO 有问题, 但不知道哪里有问题
{
    int i = 0;
    // 开始
    P3OUT &= ~BIT4; // Clear P3.4 LED off
    for(i=0; i<sample_num*3000; i++)// 3k次8次8kHz采样, 应该花费3s
    {
        sampling(i);
        int j = 0;
        while(j<sample_gap)// j<5
        {
            j++;
        }
    }
    P3OUT |= BIT4; // Set P3.4 LED on
    // 结束
    delay_nms(100);
}

void delay_1ms(void)
{
    unsigned int i;
    for (i = 0; i < 92; i++); //92
}

void delay_nms(unsigned int k)
{
    unsigned int i;
    for (i = 0; i < k; i++)
    {
        delay_1ms();
    }
}

void GPIO_Init(void)
{
    P2DIR|=BIT0+BIT1+BIT2+BIT3+BIT4+BIT5+BIT6+BIT7; // 设为输出(亮灯)
    P3DIR|=BIT0+BIT1+BIT2+BIT3+BIT4; // 设为输出(片选)
}

void timer0_init()
{
    /*
    *设置TIMER_A的时钟
    *TASSEL_0: TACLK,使用外部引脚信号作为输入
    *TASSEL_1: ACLK,辅助时钟
    *TASSEL_2: SMCLK,子系统主时钟
    *TASSEL_3: INCLK,外部输入时钟 
    */
    TACTL |= TASSEL_1;

    /*
    *时钟源分频
    *ID_0: 不分频
    *ID_1: 2分频
    *ID_2: 4分频
    *ID_3: 8分频
    */
    TACTL |= ID_0;    

    /*
    *模式选择
    *MC_0: 停止模式,用于定时器暂停
    *MC_1: 增计数模式,计数器计数到CCR0,再清零计数器
    *MC_2: 连续计数模式,计数器增计数到0XFFFF(65535),再清零计数器
    *MC_3: 增减计数模式,增计数到CCR0,再减计数到0
    */
    TACTL |= MC_1;  //增计数模式

    //----计数器清零-----
    TACTL |= TACLR; 

    //----设置TACCRx的值-----
    TACCR0 = 3277 * ref_time;  // 32768 Hz * ref_time hms
    // TACCR1=10000;         //TACCR1和TACCR2要小于TACCR0,否则不会产生中断 
    // TACCR2=20000;

    //----中断允许----
    TACCTL0 |= CCIE;      //TACCR0中断
    // TACCTL1 |= CCIE;      //TACCR1中断
    // TACCTL2 |= CCIE;      //TACCR2中断
    // TACTL |= TAIE;        //TA0溢出中断
    // P3DIR |= BIT4;
}

void adc_init(void)
{
    ADC10CTL0 = SREF_0 + ADC10SHT_2 + ADC10ON; // ADC10ON, interrupt enabled
    ADC10CTL1 = INCH_6; // Conversion code signed format, input A6
    ADC10AE0 |= BIT6;   // P1.6 ADC option select
    ADC10CTL0 |= ENC;
}

void sampling(int n)
{
    ADC10CTL0 |= ADC10SC; // Sampling and conversion start
    while ((ADC10CTL1 & ADC10BUSY) == 0x01);   // wait for conversion to end
    sample_points[n] = ADC10MEM;
    /*
    if ((int)ADC10MEM < 0)
    {
        P3OUT &= ~BIT4; // Clear P1.0 LED off
    }
    else
    {
        P3OUT |= BIT4; // Set P1.0 LED on
    }
    */
}

