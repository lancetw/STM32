/*
 * TP 控制 IC XPT2046 应用BSP
 */
 
 
#include "bsp_touch.h"
#include "bsp_gpio_spi.h"
#include "bsp_ili9341_lcd.h"
#include "bsp_SysTick.h"
#include "bsp_spi_flash.h"

extern volatile unsigned char touch_flag;

/* 触摸屏校正系数 */
long double aa1=0,bb1=0,cc1=0,aa2=0,bb2=0,cc2=0;

/* 触摸屏校正系数结构体 */
long double cal_p[6]={0};

/* 触摸屏校正标志位 */
uint8_t cal_flag = 0xcc;

/* 差值门限 */
#define THRESHOLD 2 

/*--------------------------------------------------------------------*/
// 四个重要的结构体变量

/* 触摸采样AD值保存结构体 */
Coordinate ScreenSample[4];

/* LCD逻辑坐标，用来校正时画十字用 */
//Coordinate DisplaySample[4] =   
//{
//    { 35,  35 },
//    { 35,  200},
//    { 290, 200},
//    { 290, 35}
//};
Coordinate DisplaySample[4] =   
{
    { 45,  35 },
    { 10,  200},
    { 290, 200},
    { 200, 35}
};
/* 用于保存校正系数 */
Parameter   touch_para ;

/* 液晶计算坐标，用于真正画点的时候用 */
Coordinate  display ;
/*------------------------------------------------------------------*/

/* 
 * 触摸模拟SPI IO 和 中断 IO 初始化
 */
void Touch_Init(void)
{
  GPIO_SPI_Config();
}

/*
 * us 级别延时，不是很精确
 */
static void DelayUS(vu32 cnt)
{
    uint32_t i;
    for(i = 0;i<cnt;i++)
    {
        uint8_t us = 12; /* 设置值为12，大约延1微秒 */    
        while (us--)     /* 延1微秒	*/
        {
            ;   
        }
    }
}

/*
*********************************************************************************************************
*	函 数 名: XPT2046_WriteCMD
*	功能说明: 写命令
*	形    参：CHX 	0x90 	//通道Y+的选择控制字 CHY 	0xd0	//通道X+的选择控制字
*	返 回 值: 无
*********************************************************************************************************
*/
static void XPT2046_WriteCMD(unsigned char cmd) 
{
    unsigned char buf;
    unsigned char i;
//     TP_CS(1);
    TP_DIN(0);
    TP_DCLK(0);
//     TP_CS(0);
    for(i=0;i<8;i++) 
    {
        buf=(cmd>>(7-i))&0x1;
        TP_DIN(buf);
        //Delayus(5);
      DelayUS(5);
        TP_DCLK(1);
        //Delayus(5);
      DelayUS(5);
        TP_DCLK(0);
    }
}

/*
*********************************************************************************************************
*	函 数 名: XPT2046_ReadCMD
*	功能说明: 选择一个模拟通道，启动ADC，并返回ADC采样结果
*	形    参：无
*	返 回 值: 无
*********************************************************************************************************
*/

static unsigned short XPT2046_ReadCMD(void) 
{
    unsigned short buf=0,temp;
    unsigned char i;
    TP_DIN(0);
    TP_DCLK(1);
    for(i=0;i<12;i++) 
    {
        TP_DCLK(0);          
        temp= (TP_DOUT) ? 1:0;
        buf|=(temp<<(11-i));
        TP_DCLK(1);
    }
    buf&=0x0fff;

    return(buf);
}

/*
*********************************************************************************************************
*	函 数 名: TSC2046_ReadAdc
*	功能说明: 选择一个模拟通道，启动ADC，并返回ADC采样结果
*	形    参：_ucCh = 0x90 表示Y通道； 0xd0 表示X通道
*	返 回 值: 12位ADC值
*********************************************************************************************************
*/
uint16_t XPT2046_ReadAdc(uint8_t _ucCh)
{
	  //uint16_t usAdc;

    XPT2046_WriteCMD(_ucCh);

	return 	XPT2046_ReadCMD();
}


/*
 * 校正触摸时画十字专用 
 * x:0~300
 * y:0~230
 */
void DrawCross(uint16_t x,uint16_t y)
{
   LCD_Clear(x, y, 20, 1, BLUE);
   LCD_Clear(x+10, y-10, 1, 20, BLUE);
}

/*
 * 读取TP x y 的AD值(12bit，最大是4096)
 */
void Touch_GetAdXY(int *x,int *y)  
{ 
    int adx,ady; 
    //adx=Read_X();
    adx = XPT2046_ReadAdc(CHX);
  
    DelayUS(1); 
    //ady=Read_Y();
    ady = XPT2046_ReadAdc(CHY);  
    *x=adx; 
    *y=ady; 
}

/******************************************************
* 函数名：Read_2046
* 描述  ：得到滤波之后的X Y
* 输入  : 无
* 输出  ：Coordinate结构体地址
* 举例  ：无
* 注意  ：速度相对比较慢
*********************************************************/    
Coordinate *Read_2046(void)
{
    static Coordinate  screen;
    int m0,m1,m2,TP_X[1],TP_Y[1],temp[3];
    uint8_t count=0;
		
		/* 坐标X和Y进行9次采样*/
    int buffer[2][9]={{0},{0}};  	
    do
    {		   
        Touch_GetAdXY(TP_X,TP_Y);  
        buffer[0][count]=TP_X[0];  
        buffer[1][count]=TP_Y[0];
        count++; 
				 
    }	/*用户点击触摸屏时即TP_INT_IN信号为低 并且 count<9*/
    while(!INT_IN_2046&& count<9);  
    
		
		/*如果触笔弹起*/
		if(INT_IN_2046 ==1)
    {
        /*中断标志复位*/
				touch_flag =0;						
    }

		/* 如果成功采样9次,进行滤波 */ 
    if(count==9)   								
    {  
        /* 为减少运算量,分别分3组取平均值 */
        temp[0]=(buffer[0][0]+buffer[0][1]+buffer[0][2])/3;
        temp[1]=(buffer[0][3]+buffer[0][4]+buffer[0][5])/3;
        temp[2]=(buffer[0][6]+buffer[0][7]+buffer[0][8])/3;
        
        /* 计算3组数据的差值 */
        m0=temp[0]-temp[1];
        m1=temp[1]-temp[2];
        m2=temp[2]-temp[0];
        
        /* 对上述差值取绝对值 */
        m0=m0>0?m0:(-m0);
        m1=m1>0?m1:(-m1);
        m2=m2>0?m2:(-m2);
        
        /* 判断绝对差值是否都超过差值门限，如果这3个绝对差值都超过门限值，则判定这次采样点为野点,抛弃采样点，差值门限取为2 */
        if( m0>THRESHOLD  &&  m1>THRESHOLD  &&  m2>THRESHOLD ) 
            return 0;
        
        /* 计算它们的平均值，同时赋值给screen */ 
        if(m0<m1)
        {
            if(m2<m0) 
                screen.x=(temp[0]+temp[2])/2;
            else 
                screen.x=(temp[0]+temp[1])/2;	
        }
        else if(m2<m1) 
            screen.x=(temp[0]+temp[2])/2;
        else 
            screen.x=(temp[1]+temp[2])/2;
        
        /* 同上 计算Y的平均值 */
        temp[0]=(buffer[1][0]+buffer[1][1]+buffer[1][2])/3;
        temp[1]=(buffer[1][3]+buffer[1][4]+buffer[1][5])/3;
        temp[2]=(buffer[1][6]+buffer[1][7]+buffer[1][8])/3;
        m0=temp[0]-temp[1];
        m1=temp[1]-temp[2];
        m2=temp[2]-temp[0];
        m0=m0>0?m0:(-m0);
        m1=m1>0?m1:(-m1);
        m2=m2>0?m2:(-m2);
        if(m0>THRESHOLD&&m1>THRESHOLD&&m2>THRESHOLD) 
            return 0;
        
        if(m0<m1)
        {
            if(m2<m0) 
                screen.y=(temp[0]+temp[2])/2;
            else 
                screen.y=(temp[0]+temp[1])/2;	
        }
        else if(m2<m1) 
            screen.y=(temp[0]+temp[2])/2;
        else
            screen.y=(temp[1]+temp[2])/2;
        
        return &screen;
    }
    
    else if(count>1)
    {
        screen.x=buffer[0][0];
        screen.y=buffer[1][0];
        return &screen;
    }  
    return 0; 
}

/******************************************************
* 函数名：Read_2046
* 描述  ：得到简单滤波之后的X Y
* 输入  : 无
* 输出  ：Coordinate结构体地址
* 举例  ：无
* 注意  ：”画板应用实例"专用,不是很精准，但是速度比较快
*********************************************************/    
Coordinate *Read_2046_2(void)
{
    static Coordinate  screen2;
    int TP_X[1],TP_Y[1];
    uint8_t count=0;
    int buffer[2][10]={{0},{0}};  /*坐标X和Y进行多次采样*/
    int min_x,max_x;
    int min_y,max_y;
    int	i=0;
    
#if 0 //采样5次
    do					       				
    {		/* 循环采样5次 */   
        Touch_GetAdXY(TP_X,TP_Y);  
        buffer[0][count]=TP_X[0];  
        buffer[1][count]=TP_Y[0];
        count++;  
    }	/*用户点击触摸屏时即TP_INT_IN信号为低 并且 count<10*/
    while(!INT_IN_2046&& count<5);
    
		/*如果触笔弹起*/
    if(INT_IN_2046)						
    {
				/*中断标志复位*/
        touch_flag = 0;					 
    }
		
		/*如果成功采样10个样本*/
    if(count ==5)		 					
    {
        max_x=min_x=buffer[0][0];
        max_y=min_y=buffer[1][0];       
        for(i=1; i<5; i++)
        {
            if(buffer[0][i]<min_x)
            {
                min_x=buffer[0][i];
            }
            else
            if(buffer[0][i]>max_x)
            {
                max_x = buffer[0][i];
            }
        }
        
        for(i=1; i<5; i++)
        {
            if(buffer[1][i]<min_y)
            {
                min_y=buffer[1][i];
            }
            else
            if(buffer[1][i]>max_y)
            {
                max_y = buffer[1][i];
            }
        }
				/*去除最小值和最大值之后求平均值*/
        screen2.x=(buffer[0][0]+buffer[0][1]+buffer[0][2]+buffer[0][3]+buffer[0][4]-min_x-max_x)/3;
        screen2.y=(buffer[1][0]+buffer[1][1]+buffer[1][2]+buffer[1][3]+buffer[1][4]-min_y-max_y)/3; 
        
        return &screen2;
    }
#endif //采样5次		
	

#if 1
    do					       				
    {		/* 循环采样10次 */   
        Touch_GetAdXY(TP_X,TP_Y);  
        buffer[0][count]=TP_X[0];  
        buffer[1][count]=TP_Y[0];
        count++;  
    }	/*用户点击触摸屏时即TP_INT_IN信号为低 并且 count<10*/
    while(!INT_IN_2046&& count<10);
    
		/*如果触笔弹起*/
    if(INT_IN_2046)						
    {
				/*中断标志复位*/
        touch_flag = 0;					 
    }
		
		/*如果成功采样10个样本*/
    if(count ==10)		 					
    {
        max_x=min_x=buffer[0][0];
        max_y=min_y=buffer[1][0];       
        for(i=1; i<10; i++)
        {
            if(buffer[0][i]<min_x)
            {
                min_x=buffer[0][i];
            }
            else
            if(buffer[0][i]>max_x)
            {
                max_x = buffer[0][i];
            }
        }
        
        for(i=1; i<10; i++)
        {
            if(buffer[1][i]<min_y)
            {
                min_y=buffer[1][i];
            }
            else
            if(buffer[1][i]>max_y)
            {
                max_y = buffer[1][i];
            }
        }
				/*去除最小值和最大值之后求平均值*/
        screen2.x=(buffer[0][0]+buffer[0][1]+buffer[0][2]+buffer[0][3]+buffer[0][4]+buffer[0][5]+buffer[0][6]+buffer[0][7]+buffer[0][8]+buffer[0][9]-min_x-max_x)>>3;
        screen2.y=(buffer[1][0]+buffer[1][1]+buffer[1][2]+buffer[1][3]+buffer[1][4]+buffer[1][5]+buffer[1][6]+buffer[1][7]+buffer[1][8]+buffer[1][9]-min_y-max_y)>>3; 
        
        return &screen2;
    }   
#endif		
    return 0;    
}

/******************************************************
* 函数名：Cal_touch_para
* 描述  ：计算出触摸屏到液晶屏坐标变换的转换函数的 K A B C D E F系数
* 输入  : 无
* 输出  ：返回1表示成功 0失败
* 举例  ：无
* 注意  ：只有在LCD和触摸屏间的误差角度非常小时,才能运用下面公式
*********************************************************/    
FunctionalState Cal_touch_para( Coordinate * displayPtr,
                                Coordinate * screenPtr,
                                Parameter * para)
{
    
    FunctionalState retTHRESHOLD = ENABLE ;

    /* K＝(X0－X2) (Y1－Y2)－(X1－X2) (Y0－Y2) */
    para->Divider = ((screenPtr[0].x - screenPtr[2].x) * (screenPtr[1].y - screenPtr[2].y)) - 
                    ((screenPtr[1].x - screenPtr[2].x) * (screenPtr[0].y - screenPtr[2].y)) ;
    
		if( para->Divider == 0 )
    {
        retTHRESHOLD = DISABLE;
    }
    else
    {
        /* A＝((XD0－XD2) (Y1－Y2)－(XD1－XD2) (Y0－Y2))／K	*/
        para->An = ((displayPtr[0].x - displayPtr[2].x) * (screenPtr[1].y - screenPtr[2].y)) - 
                   ((displayPtr[1].x - displayPtr[2].x) * (screenPtr[0].y - screenPtr[2].y));
        
        /* B＝((X0－X2) (XD1－XD2)－(XD0－XD2) (X1－X2))／K	*/
        para->Bn = ((screenPtr[0].x - screenPtr[2].x) * (displayPtr[1].x - displayPtr[2].x)) - 
                   ((displayPtr[0].x - displayPtr[2].x) * (screenPtr[1].x - screenPtr[2].x));
        
        /* C＝(Y0(X2XD1－X1XD2)+Y1(X0XD2－X2XD0)+Y2(X1XD0－X0XD1))／K */
        para->Cn = (screenPtr[2].x * displayPtr[1].x - screenPtr[1].x * displayPtr[2].x) * screenPtr[0].y +
                   (screenPtr[0].x * displayPtr[2].x - screenPtr[2].x * displayPtr[0].x) * screenPtr[1].y +
                   (screenPtr[1].x * displayPtr[0].x - screenPtr[0].x * displayPtr[1].x) * screenPtr[2].y ;
        
        /* D＝((YD0－YD2) (Y1－Y2)－(YD1－YD2) (Y0－Y2))／K	*/
        para->Dn = ((displayPtr[0].y - displayPtr[2].y) * (screenPtr[1].y - screenPtr[2].y)) - 
                   ((displayPtr[1].y - displayPtr[2].y) * (screenPtr[0].y - screenPtr[2].y)) ;
        
        /* E＝((X0－X2) (YD1－YD2)－(YD0－YD2) (X1－X2))／K	*/
        para->En = ((screenPtr[0].x - screenPtr[2].x) * (displayPtr[1].y - displayPtr[2].y)) - 
                   ((displayPtr[0].y - displayPtr[2].y) * (screenPtr[1].x - screenPtr[2].x)) ;
        
        
        /* F＝(Y0(X2YD1－X1YD2)+Y1(X0YD2－X2YD0)+Y2(X1YD0－X0YD1))／K */
        para->Fn = (screenPtr[2].x * displayPtr[1].y - screenPtr[1].x * displayPtr[2].y) * screenPtr[0].y +
                   (screenPtr[0].x * displayPtr[2].y - screenPtr[2].x * displayPtr[0].y) * screenPtr[1].y +
                   (screenPtr[1].x * displayPtr[0].y - screenPtr[0].x * displayPtr[1].y) * screenPtr[2].y;
        
    }
    return( retTHRESHOLD ) ;
}

/******************************************************
* 函数名：Touchl_Calibrate
* 描述  ：触摸屏校正函数
* 输入  : 无
* 输出  ：0	---	校正成功
					1	---	校正失败
* 举例  ：无
* 注意  ：无
*********************************************************/ 
int Touch_Calibrate(void)
{
    uint8_t i,k;
    u16 test_x=0, test_y=0;
    u16 gap_x=0, gap_y=0;
    Coordinate * Ptr;   
    
		Lcd_GramScan(2);
    for(i=0; i<4; i++)
    {        
        LCD_Clear(0, 0, 320, 240, BACKGROUND);        
        LCD_DispStr(110, 110, (uint8_t *)"Touch Calibrate......", BLUE);	         
        LCD_DisNum(160, 90, i+1, BLUE);
      
        /* 适当的延时很有必要 */        
			//	Delay_ms(500);   
				DelayUS(0x3ffff);
        DrawCross(DisplaySample[i].x,DisplaySample[i].y);  //显示校正用的“十”字
        do
        {
            Ptr=Read_2046_2();  //读取TSC2046数据到变量ptr            
        }
        while( Ptr == (void*)0 );     //当ptr为空时表示没有触点被按下
        ScreenSample[i].x= Ptr->x; 	  //把读取的原始数据存放到全局变量ScreenSample结构体
        ScreenSample[i].y= Ptr->y;

    }
		/* 用原始参数计算出 原始参数与坐标的转换系数。 */
    Cal_touch_para( &DisplaySample[0],&ScreenSample[0],&touch_para ) ;  	   
    
		/*取一个点计算X值*/
    test_x = ( (touch_para.An * ScreenSample[3].x) + 
               (touch_para.Bn * ScreenSample[3].y) + 
                touch_para.Cn 
             ) / touch_para.Divider ;			 
    
		/*取一个点计算Y值*/
    test_y = ( (touch_para.Dn * ScreenSample[3].x) + 
               (touch_para.En * ScreenSample[3].y) + 
               touch_para.Fn 
             ) / touch_para.Divider ;
    
    /* 实际坐标与计算坐标的差 */
    gap_x = (test_x > DisplaySample[3].x)?(test_x - DisplaySample[3].x):(DisplaySample[3].x - test_x);
    gap_x = (test_y > DisplaySample[3].y)?(test_y - DisplaySample[3].y):(DisplaySample[3].y - test_y);
    

    //LCD_Rectangle(0,0,320,240,CAL_BACKGROUND_COLOR);
    LCD_Clear(0, 0, 320, 240, BACKGROUND);
    
    /* 可以通过修改这两个值的大小来调整精度 */
    if((gap_x>10)||(gap_y>10))
    {
      LCD_DispStr(100, 100, (uint8_t *)"Calibrate fail", BLUE);
      LCD_DispStr(100, 120, (uint8_t *)"try again", BLUE);     
     // Delay_ms(2000);
			DelayUS(0x1ffff);
      return 1;
    }    
    
    /* 校正系数为全局变量 */
//    aa1 = (touch_para.An*1.0)/touch_para.Divider;
//    bb1 = (touch_para.Bn*1.0)/touch_para.Divider;
//    cc1 = (touch_para.Cn*1.0)/touch_para.Divider;
//    
//    aa2 = (touch_para.Dn*1.0)/touch_para.Divider;
//    bb2 = (touch_para.En*1.0)/touch_para.Divider;
//    cc2 = (touch_para.Fn*1.0)/touch_para.Divider; 

    cal_p[0] = (touch_para.An*1.0)/touch_para.Divider;  // aa1
    cal_p[1] = (touch_para.Bn*1.0)/touch_para.Divider;  // bb1
    cal_p[2] = (touch_para.Cn*1.0)/touch_para.Divider;  // cc1
    
    cal_p[3] = (touch_para.Dn*1.0)/touch_para.Divider;  // aa2
    cal_p[4] = (touch_para.En*1.0)/touch_para.Divider;  // bb2
    cal_p[5] = (touch_para.Fn*1.0)/touch_para.Divider;  // cc2     
 
    {
      cal_flag = 0x45;
      SPI_FLASH_SectorErase(0);
      SPI_FLASH_BufferWrite(&cal_flag, 0, 1);     
      SPI_FLASH_BufferWrite((void*)cal_p, 1, sizeof(cal_p));
      for( k=0; k<6; k++ )
        printf("\r\n tx = %LF \r\n",cal_p[k]);
    }
    
    LCD_DispStr(100, 100, (uint8_t *)"Calibrate Succed", BLUE);  
   // Delay_ms(1000);
		DelayUS(0x1ffff);
    return 0;    
}

/*
 * 画板初始化，用于取色用
 */
void Palette_Init(void)
{
  /* 整屏清为白色 */
  LCD_Clear(0, 0, 320, 240, WHITE);
  
  /* 画两条直线 */
  LCD_Clear(39, 0, 1, 30, BLACK);
  LCD_Clear(0, 29, 40, 1, BLACK);
  LCD_DispStr(7, 10, (uint8_t *)"CLR", BLUE);
  
  LCD_Clear(0, 30, 40, 30, GREEN);
  LCD_Clear(0, 60, 40, 30, BLUE);
  LCD_Clear(0, 90, 40, 30, BRED);
  LCD_Clear(0, 120, 40, 30, GRED);
  LCD_Clear(0, 150, 40, 30, GBLUE);
  LCD_Clear(0, 180, 40, 30, BLACK);
  LCD_Clear(0, 210, 40, 30, RED);  
  
 // Delay_ms(500);
	DelayUS(0x1ffff);
}

/******************************************************
* 函数名：Get_touch_point
* 描述  ：通过 K A B C D E F 把通道X Y的值转换为液晶屏坐标
* 输入  : 无
* 输出  ：返回1表示成功 0失败
* 举例  ：无
* 注意  ：如果获取的触点信息有误，将返回DISABLE
*********************************************************/    
//long double linear=0 ;
//long double aa1=0,bb1=0,cc1=0,aa2=0,bb2=0,cc2=0;
FunctionalState Get_touch_point(Coordinate * displayPtr,
                                Coordinate * screenPtr,
                                Parameter * para )
{
  FunctionalState retTHRESHOLD =ENABLE ;

  if(screenPtr==0)
  {
    /*如果获取的触点信息有误，则返回DISABLE*/
    retTHRESHOLD = DISABLE;			
  }
  else
  {    
    //if( para->Divider != 0 )  /* 每次都要校正时 */
    if( para->Divider != 1 )    /* 校正系数写到FLASH时 */
    {        
      //displayPtr->x = ( (aa1 * screenPtr->x) + (bb1 * screenPtr->y) + cc1);        
      //displayPtr->y = ((aa2 * screenPtr->x) + (bb2 * screenPtr->y) + cc2 );
      displayPtr->x = ( (cal_p[0] * screenPtr->x) + (cal_p[1] * screenPtr->y) + cal_p[2]);        
      displayPtr->y = ((cal_p[3] * screenPtr->x) + (cal_p[4] * screenPtr->y) + cal_p[5] );
    }
    else
    {
      retTHRESHOLD = DISABLE;
    }
  }
  return(retTHRESHOLD);
} 

/******************************************************
* 函数名：Palette_draw_point
* 描述  ：在LCD指定位置画一个大点(包含四个小点)
* 输入  : Xpos		--X方向位置
*         Ypos		--Y方向位置
* 输出  ：无
* 举例  ：Palette_draw_point(100,100);
* 注意  ：该函数是 "触摸画板应用实例" 专用函数
*********************************************************/    
void Palette_draw_point(uint16_t x, uint16_t y)
{
  /* 画笔默认为黑色 */
  static u16 Pen_color=0; 
  u16 y_pos = y;

  /* 在画板内取色 */
  if( x<40 )
  {
    if( y>30 )
    Pen_color = (y_pos<60)?GREEN:\
                (y_pos<90)?BLUE:\
                (y_pos<120)?BRED:\
                (y_pos<150)?GRED:\
                (y_pos<180)?GBLUE:\
                (y_pos<210)?BLACK:\
                (y_pos<240)?RED:BLUE;
    else
    {/* 清屏 */
      #if 0
      LCD_Clear(40, 0, 280, 240, WHITE);
      #elif 1
      LCD_Clear(40, 0, 280, 240, BLACK);
      #endif
      return;
    }
  }
  else
  {
    LCD_SetPoint(x , y , Pen_color);
    LCD_SetPoint(x+1 , y , Pen_color);
    LCD_SetPoint(x , y+1 , Pen_color);
    LCD_SetPoint(x+1 , y+1 , Pen_color);
  }	
}













