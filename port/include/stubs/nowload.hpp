#pragma once

struct NowLoadingInfo;
extern NowLoadingInfo LoadInfo asm("NowLoad_LoadInfo");
extern int            ProgBarCnt asm("NowLoad_ProgBarCnt");
extern float          ProgBarWidthStep asm("NowLoad_ProgBarWidthStep");

// game.skip_intro, which SCElogoFade's fade loop checks.
bool PortSkipIntro();
// game.language: the language to start in without asking, or 0 to show the language menu.
int PortSavedLanguage();
// Stores the language chosen on the language menu in game.language.
void PortRememberLanguage(int language);
