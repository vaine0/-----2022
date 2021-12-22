/*
 * ������Ϣ����������
 * date: 2021/12/09
 * author: vaine
 * describe: ��ѭ����ʱ��ʹ��display_THD()���� �����������ʾȫ�ֱ���THD��ֵ;
 *           �ڼ�ʱ���жϺ�����ʹ��refresh_THD()������ʱˢ��THD��ֵ.
 * display_THD(): �������ʾ float THD (ֻ��ʾ ��λ & С������ǰ��λ);
 *                �������ڸ��������ȳ������!=����, ��������0.001
 * refresh_THD(): ˢ��THD��ֵ.
 *                �ȶ�ȡsample_num������ֵ(����Ƶ��sample_num*1kHz), ����ʽ��Ϊfloat;
 *                ��DFT�õ���Ƶ�ʷ���; ������Ƶ�ʷ�������THD(����ʹ�ö��ַ�)
 */

#include <msp430g2553.h>

// ��������
#define sample_num 8
// �ı�sample_num��Ҫ�ֶ�����sample_gap, ʹ����Ƶ�ʴﵽsample_num*1kHz
const unsigned int sample_gap = 5; // 5��Ӧ����Ƶ��8kHz

const unsigned char display_pos[3] = {BIT1, BIT2, BIT3};
const unsigned char display_num[10] = {0x03, 0x9F, 0x25, 0x0D, 0x99, 0x49, 0x41, 0x1F, 0x01, 0x09};
const float Cos[sample_num] = {1.000, 0.707, 0.000, -0.707, -1.000, -0.707, 0.000, 0.707};
const float Sin[sample_num] = {0.000, 0.707, 1.000, 0.707, 0.000, -0.707, -1.000, -0.707};
const unsigned int  ref_time  = 1000 / 100;      // THDˢ��ʱ����; n ms / 100 (��ֹ���)
const float ref_vcc = 3300.0; // ad�ο���ѹ

float THD = 0.000; // ȫ�ֱ���THD, display_THD()ʼ����ʾTHD; ���㺯��refresh_THD()�ڼ�ʱ�жϽ���, ���ϸ�����õ�THD
int sample_points[sample_num];
float sample_nums[sample_num];

/*
 * 1ms��ʱ����
 */
void delay_1ms(void)
{
    unsigned int i;
    for (i = 0; i < 92; i++); //92
}

/*
 * nms��ʱ����
 */
void delay_nms(unsigned int k)
{
    unsigned int i;
    for (i = 0; i < k; i++)
    {
        delay_1ms();
    }
}

/*
 * ���ų�ʼ������
 */
void GPIO_Init(void)
{
    P2DIR|=BIT0+BIT1+BIT2+BIT3+BIT4+BIT5+BIT6+BIT7; // ��Ϊ���(����)
    P3DIR|=BIT0+BIT1+BIT2+BIT3+BIT4; // ��Ϊ���(Ƭѡ)
}

/*
 * ��ʱ����ʼ��
 */
void timer0_init()
{
    /*
    *����TIMER_A��ʱ��
    *TASSEL_0: TACLK,ʹ���ⲿ�����ź���Ϊ����
    *TASSEL_1: ACLK,����ʱ��
    *TASSEL_2: SMCLK,��ϵͳ��ʱ��
    *TASSEL_3: INCLK,�ⲿ����ʱ�� 
    */
    TACTL |= TASSEL_1;

    /*
    *ʱ��Դ��Ƶ
    *ID_0: ����Ƶ
    *ID_1: 2��Ƶ
    *ID_2: 4��Ƶ
    *ID_3: 8��Ƶ
    */
    TACTL |= ID_0;    

    /*
    *ģʽѡ��
    *MC_0: ֹͣģʽ,���ڶ�ʱ����ͣ
    *MC_1: ������ģʽ,������������CCR0,�����������
    *MC_2: ��������ģʽ,��������������0XFFFF(65535),�����������
    *MC_3: ��������ģʽ,��������CCR0,�ټ�������0
    */
    TACTL |= MC_1;  //������ģʽ

    //----����������-----
    TACTL |= TACLR; 

    //----����TACCRx��ֵ-----
    TACCR0 = 3277 * ref_time;  // 32768 Hz * ref_time hms
    // TACCR1=10000;         //TACCR1��TACCR2ҪС��TACCR0,���򲻻�����ж� 
    // TACCR2=20000;

    //----�ж�����----
    TACCTL0 |= CCIE;      //TACCR0�ж�
    // TACCTL1 |= CCIE;      //TACCR1�ж�
    // TACCTL2 |= CCIE;      //TACCR2�ж�
    // TACTL |= TAIE;        //TA0����ж�
    // P3DIR |= BIT4;
}

/*
 * ADת����ʼ��
 */
void adc_init(void)
{
    ADC10CTL0 = 0xA0 + ADC10SHT_2 + ADC10ON + ADC10IE; // ADC10ON, interrupt enabled
    ADC10CTL1 = ADC10DF + INCH_6; 		// Conversion code signed format, input A6
    ADC10AE0 |= BIT6;				// P1.6 ADC option select
    ADC10CTL0 |= ENC;
}

/*
 * ����һ��, ���������������sample_points[n]
 */
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

/*
 * ˢ��THD.
 * �ȶ�ȡsample_num������ֵ
 * ��DFT�õ���Ƶ�ʷ���
 * ������Ƶ�ʷ�������THD
 */
void refresh_THD(void)
{
    P2OUT = 0xFF;
    P3OUT = BIT0+BIT1+BIT2+BIT3;
    P2OUT = 0xFD;
    // 8kHz����8��, �������sample_points[]
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
    // ��������ֵ��Ϊ����������sample_nums[]
    for(i=0; i<sample_num; i++)
    {
        sample_nums[i] = (float)(sample_points[i]>>6) / 1023.0 * ref_vcc;
    }
    // DFT��ȡƵ�ʷ���
    float real[5] = {0.00, 0.00, 0.00, 0.00, 0.00};
    float imag[5] = {0.00, 0.00, 0.00, 0.00, 0.00};
    int k = 0;
    int n = 0;
    for(k=1; k<5; k++)
    {
        for(n=0; n<8; n++)
        {
        real[k] += sample_nums[n]*Cos[n*k];
        imag[k] += sample_nums[n]*Sin[n*k];
        }
    }
    // ����THD
    THD =  real[2]*real[2] + real[3]*real[3] + real[4]*real[4];
    THD += imag[2]*imag[2] + imag[3]*imag[3] + imag[4]*imag[4];
    THD /= real[1]*real[1] + imag[1]*imag[1];
    // TODO ������
    // THD = sample_nums[0];
    P2OUT = 0xFF;
    P3OUT = 0x00;
}


/*
 * ��ʱ���ж���Ӧ����
 */
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer0_A0(void)
{
    refresh_THD();
    // P3OUT ^= BIT4;
}


/*
 * �������ʾ float THD (ֻ��ʾ ��λ & С������ǰ��λ)
 * 1 > THD >= 0
 * �������ڸ��������ȳ������!=����, ��������0.001
 */
void display_THD(void)
{
    float n = THD;
    unsigned int temp = (int)n;
    n -= temp;
    P2OUT = 0xFF; // ���ǰȫ��
    P3OUT = BIT0; // ��һλ
    P2OUT = display_num[temp]; // ��ʾ"x"
    P2OUT &= 0xFE;             // ��ʾС����
    delay_nms(10);
    unsigned int i;
    for (i = 0; i < 3; i++)
    {
        P2OUT = 0xFF; // ���ǰȫ��
        P3OUT = display_pos[i];
        n = n * 10;
        unsigned int temp = (int)n;
        P2OUT = display_num[temp];
        n -= temp;
        delay_nms(10);
    }
}


/*
 * ����ʱ��Ƶ��, �ҵ��ܹ��ﵽ����Ƶ�ʵ��ӳ�
 */
void clock_test(void)
{
    int i = 0;
    // ��ʼ
    P3OUT &= ~BIT4; // Clear P3.4 LED off
    for(; i<sample_num*3000; i++)// 3k��8��8kHz����, Ӧ�û���3s
    {
        sampling(i);
        int j = 0;
        while(j<5)// j<5
        {
        j++;
        }
    }
    P3OUT |= BIT4; // Set P3.4 LED on
    // ����
    delay_nms(100);
}


/*
 * ������
 */
int main(void)
{
    WDTCTL = WDTPW | WDTHOLD; // stop watchdog timer
    GPIO_Init();
    adc_init();
    timer0_init();
    _EINT();          //�����ж�
    delay_nms(500);
    while(1)
    {
        display_THD();
        //clock_test();
    }
}

