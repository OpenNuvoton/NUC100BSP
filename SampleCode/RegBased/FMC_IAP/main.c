/**************************************************************************//**
 * @file     FMC_IAP.c
 * @version  V2.00
 * $Revision: 2 $
 * $Date: 15/04/13 3:29p $
 * @brief
 *           Show how to call LDROM functions from APROM.
 *           The code in APROM will look up the table at 0x100E00 to get the address of function of LDROM and call the function.
 *
 * @note
 * @copyright SPDX-License-Identifier: Apache-2.0
 *
 * @copyright Copyright (C) 2014 Nuvoton Technology Corp. All rights reserved.
 ******************************************************************************/
#include <stdio.h>
#include "NUC100Series.h"

#define PLLCON_SETTING      CLK_PLLCON_50MHz_HXT
#define PLL_CLOCK           50000000

#define KEY_ADDR    0x20000FFC      /* The location of signature */
#define SIGNATURE   0x21557899      /* The signature word is used by AP code to check if simple LD is finished */

extern uint32_t loaderImageBase;
extern uint32_t loaderImageLimit;

uint32_t g_u32ImageSize;

uint32_t *g_au32funcTable = (uint32_t *)0x100e00; /* The location of function table */

void SYS_Init(void)
{
    uint32_t u32TimeOutCnt;

    /*---------------------------------------------------------------------------------------------------------*/
    /* Init System Clock                                                                                       */
    /*---------------------------------------------------------------------------------------------------------*/

    /* Enable Internal RC 22.1184MHz clock */
    CLK->PWRCON |= CLK_PWRCON_OSC22M_EN_Msk;

    /* Waiting for Internal RC clock ready */
    u32TimeOutCnt = SystemCoreClock; /* 1 second time-out */
    while(!(CLK->CLKSTATUS & CLK_CLKSTATUS_OSC22M_STB_Msk))
        if(--u32TimeOutCnt == 0) return;

    /* Switch HCLK clock source to Internal RC and and HCLK source divide 1 */
    CLK->CLKSEL0 &= ~CLK_CLKSEL0_HCLK_S_Msk;
    CLK->CLKSEL0 |= CLK_CLKSEL0_HCLK_S_HIRC;
    CLK->CLKDIV &= ~CLK_CLKDIV_HCLK_N_Msk;
    CLK->CLKDIV |= CLK_CLKDIV_HCLK(1);

    /* Enable external XTAL 12MHz clock */
    CLK->PWRCON |= CLK_PWRCON_XTL12M_EN_Msk;

    /* Waiting for external XTAL clock ready */
    u32TimeOutCnt = SystemCoreClock; /* 1 second time-out */
    while(!(CLK->CLKSTATUS & CLK_CLKSTATUS_XTL12M_STB_Msk))
        if(--u32TimeOutCnt == 0) return;

    /* Set core clock as PLL_CLOCK from PLL */
    CLK->PLLCON = PLLCON_SETTING;

    /* Waiting for PLL ready */
    u32TimeOutCnt = SystemCoreClock; /* 1 second time-out */
    while(!(CLK->CLKSTATUS & CLK_CLKSTATUS_PLL_STB_Msk))
        if(--u32TimeOutCnt == 0) return;

    CLK->CLKSEL0 &= (~CLK_CLKSEL0_HCLK_S_Msk);
    CLK->CLKSEL0 |= CLK_CLKSEL0_HCLK_S_PLL;

    /* Update System Core Clock */
    /* User can use SystemCoreClockUpdate() to calculate PllClock, SystemCoreClock and CycylesPerUs automatically. */
    //SystemCoreClockUpdate();
    PllClock        = PLL_CLOCK;            // PLL
    SystemCoreClock = PLL_CLOCK / 1;        // HCLK
    CyclesPerUs     = PLL_CLOCK / 1000000;  // For SYS_SysTickDelay()

    /* Enable UART module clock */
    CLK->APBCLK |= CLK_APBCLK_UART0_EN_Msk;

    /* Select UART module clock source */
    CLK->CLKSEL1 &= ~CLK_CLKSEL1_UART_S_Msk;
    CLK->CLKSEL1 |= CLK_CLKSEL1_UART_S_HXT;

    /*---------------------------------------------------------------------------------------------------------*/
    /* Init I/O Multi-function                                                                                 */
    /*---------------------------------------------------------------------------------------------------------*/
    /* Set GPB multi-function pins for UART0 RXD and TXD  */
    SYS->GPB_MFP &= ~(SYS_GPB_MFP_PB1_Msk | SYS_GPB_MFP_PB0_Msk);
    SYS->GPB_MFP |= (SYS_GPB_MFP_PB1_UART0_TXD | SYS_GPB_MFP_PB0_UART0_RXD);
}


void UART0_Init(void)
{
    /*---------------------------------------------------------------------------------------------------------*/
    /* Init UART                                                                                               */
    /*---------------------------------------------------------------------------------------------------------*/
    /* Reset UART0 */
    SYS->IPRSTC2 |=  SYS_IPRSTC2_UART0_RST_Msk;
    SYS->IPRSTC2 &= ~SYS_IPRSTC2_UART0_RST_Msk;

    /* Configure UART0 and set UART0 Baudrate */
    UART0->BAUD = UART_BAUD_MODE2 | UART_BAUD_MODE2_DIVIDER(__HXT, 115200);
    UART0->LCR = UART_WORD_LEN_8 | UART_PARITY_NONE | UART_STOP_BIT_1;
}


void FMC_LDROM_Test(void)
{
    int32_t  i32Err;
    uint32_t u32Data, i, *pu32Loader;

    /* Enable LDROM Update */
    FMC->ISPCON |= FMC_ISPCON_LDUEN_Msk;

    printf("  Erase LD ROM ............................... ");

    /* Page Erase LDROM */
    for(i = 0; i < FMC_LDROM_SIZE; i += FMC_FLASH_PAGE_SIZE)
        FMC_Erase(FMC_LDROM_BASE + i);

    /* Erase Verify */
    i32Err = 0;

    for(i = FMC_LDROM_BASE; i < (FMC_LDROM_BASE + FMC_LDROM_SIZE); i += 4)
    {
        u32Data = FMC_Read(i);

        if(u32Data != 0xFFFFFFFF)
        {
            i32Err = 1;
        }
    }

    if(i32Err)
        printf("[FAIL]\n");
    else
        printf("[OK]\n");

    printf("  Program Simple LD Code ..................... ");
    pu32Loader = (uint32_t *)&loaderImageBase;
    g_u32ImageSize = (uint32_t)&loaderImageLimit - (uint32_t)&loaderImageBase;
    for(i = 0; i < g_u32ImageSize; i += 4)
    {
        /* Erase page when necessary */
        if((i & (FMC_FLASH_PAGE_SIZE - 1)) == 0)
        {
            FMC_Erase(FMC_LDROM_BASE + i);
        }

        FMC_Write(FMC_LDROM_BASE + i, pu32Loader[i / 4]);
    }

    /* Verify loader */
    i32Err = 0;
    for(i = 0; i < g_u32ImageSize; i += 4)
    {
        u32Data = FMC_Read(FMC_LDROM_BASE + i);
        if(u32Data != pu32Loader[i / 4])
            i32Err = 1;
    }

    if(i32Err)
    {
        printf("[FAIL]\n");
    }
    else
    {
        printf("[OK]\n");
    }
}

/*---------------------------------------------------------------------------------------------------------*/
/*  Main Function                                                                                          */
/*---------------------------------------------------------------------------------------------------------*/
int32_t main(void)
{
    uint32_t u32Data;
    uint32_t u32Cfg;
    int32_t (*func)(int32_t n);
    int32_t i;

    /*
        This sample code is used to demo how IAP works.
        In other words, it shows how to call functions of LDROM code in APROM.

        The execution flow of this code is:
        1. Make sure FMC_LD.bin is existed. User can select "FMC_IAP_LD" target to build it.
        2. The code will ask user to update LDROM code. User may press 'y' to update LDROM with "FMC_IAP_LD.bin".
        3. The code will call the functions in LDROM by function table located at 0x100e00.
    */

    /* Unlock protected registers for ISP function */
    SYS_UnlockReg();

    /* Init System, IP clock and multi-function I/O */
    SYS_Init();

    /* Init UART0 for printf */
    UART0_Init();

    printf("+------------------------------------------------------------------+\n");
    printf("|                       NUC100 IAP Sample Code                     |\n");
    printf("+------------------------------------------------------------------+\n");

    printf("\nCPU @ %dHz\n\n", SystemCoreClock);


    /* Enable ISP function */
    FMC->ISPCON |= FMC_ISPCON_ISPEN_Msk;

    /* Check IAP mode */
    u32Cfg = FMC_Read(FMC_CONFIG_BASE);
    if((u32Cfg & 0xc0) != 0x80)
    {
        printf("Do you want to set to new IAP mode (APROM boot + LDROM) y/n?\n");
        if(getchar() == 'y')
        {
            FMC->ISPCON |= FMC_ISPCON_CFGUEN_Msk; /* Enable user configuration update */

            /* Set CBS to b'10 */
            u32Cfg &= ~0xc0ul;
            u32Cfg |= 0x80;
            u32Data = FMC_Read(FMC_CONFIG_BASE + 0x4); /* Backup the data of config1 */
            FMC_Erase(FMC_CONFIG_BASE);
            FMC_Write(FMC_CONFIG_BASE, u32Cfg);
            FMC_Write(FMC_CONFIG_BASE + 0x4, u32Data);

            printf("Press any key to reset system to enable new IAP mode ...\n");
            getchar();
            SYS->IPRSTC1 = 0x1; /* Reset MCU */
            while(1);
        }
        else
        {
            goto lexit;
        }
    }

    printf("Do you want to write LDROM code to 0x100000\n");

    if(getchar() == 'y')
    {
        /* Check LD image size */
        g_u32ImageSize = (uint32_t)&loaderImageLimit - (uint32_t)&loaderImageBase;

        if(g_u32ImageSize == 0)
        {
            printf("  ERROR: Loader Image is 0 bytes!\n");
            goto lexit;
        }

        if(g_u32ImageSize > FMC_LDROM_SIZE)
        {
            printf("  ERROR: Loader Image is larger than 4K Bytes!\n");
            goto lexit;
        }

        /* Erase LDROM, program LD sample code to LDROM, and verify LD sample code */
        FMC_LDROM_Test();
    }

#if defined(__GNUC__)
    for(i = 0; i < 4; i++)
    {
        /* Call the function of LDROM */
        func = (int32_t (*)(int32_t))g_au32funcTable[i];
        if(func(i + 1) == ((i + 1)*(i + 1)))
        {
            printf("Call LDROM function %d ok!\n", i);
        }
        else
        {
            printf("Call LDROM function %d fail.\n", i);
        }
    }
#else
    for(i = 0; i < 4; i++)
    {
        /* Call the function of LDROM */
        func = (int32_t (*)(int32_t))g_au32funcTable[i];
        if(func(i + 1) == i + 1)
        {
            printf("Call LDROM function %d ok!\n", i);
        }
        else
        {
            printf("Call LDROM function %d fail.\n", i);
        }
    }
 #endif

lexit:

    /* Disable ISP function */
    FMC->ISPCON &= ~FMC_ISPCON_ISPEN_Msk;

    /* Lock protected registers */
    SYS_LockReg();

    printf("\nDone\n");
    while(SYS->PDID) __WFI();
}
