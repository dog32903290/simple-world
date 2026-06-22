// MockStrings string op (string self-registration seam leaf — Category(enum) → String). TiXL
// authority: Operators/Lib/string/random/MockStrings.cs (the _strings arrays are ported VERBATIM
// below — extracted programmatically from the .cs to guarantee byte-identical content). Semantics:
//
//   MockStrings.cs Update():
//     var categoryIndex = Category.GetValue(context);
//     categoryIndex = MathUtils.Mod(categoryIndex, _strings.Length);
//     Result.Value = _strings[categoryIndex];
//
//   Category = InputSlot<int> (MappedType = Categories enum, 15 values). Output: Result = Slot<string>.
//   MathUtils.Mod(val, repeat): euclidean mod — `var x = val % repeat; if (x<0) x = repeat + x;`
//   (and returns 0 when repeat==0). So categoryIndex wraps into [0, _strings.Length) for ANY int.
//
// ★ DETERMINISTIC — NOT STATEFUL: despite living under string/random/, MockStrings has NO RNG and NO
// cross-frame state. The output is a pure function of Category (index into a fixed array). It rides
// the GENERIC resident path exactly like SubString/ChangeCase. (Confirmed against the .cs: no Random,
// no frame/time read, no _lastX field.)
//
// Categories enum ORDER (the index → array mapping, ported verbatim from MockStrings.cs:41-59):
//   0 Colors, 1 Drugs, 2 Females, 3 Males, 4 Bullshit, 5 LoremIpsum, 6 Assembler, 7 Preaching,
//   8 Demogroups, 9 ValuesToRates, 10 RegularPrimes, 11 IsolatedPrimes, 12 HappyPrimes,
//   13 FibonacciPrimes, 14 BalancedPrimes  (15 values; _strings has 15 entries → Mod base = 15).
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode) with ZERO String inputs — Category is the
// only port besides Result, an enum dissolved to Float (the value spine, Widget::Enum). It reads NO
// inputStrings; it picks _strings[Mod((int)CategoryFloat, 15)].
//
// FORKS (named):
//   - fork-int-bool-dissolve-to-float: TiXL Category is InputSlot<int> (MappedType=Categories); sw has
//     no Int/enum port type, so it dissolves enum→Float (stored as the integer index in params /
//     value spine, read back via (int)(float)). Cut32 convention.
//   - fork-mockstrings-euclidean-mod: MathUtils.Mod is euclidean (negative category wraps to a
//     non-negative index), ported 1:1 (NOT C++ truncated %, which would give a negative index → OOB).
//   - fork-mockstrings-newlines-verbatim: the C# string literals' \n escapes are real newline bytes at
//     runtime; the C++ literals below use the same \n escapes → byte-identical multi-line content.
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
#include <string>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// The 15 hardcoded string arrays, ported VERBATIM from MockStrings.cs:23-39 (extracted
// programmatically). Index = Categories enum value. \n escapes = real newlines (matching C#).
static const char* const kMockStrings[] = {
    /*  0 */ "Black\nCobalt\nTeal\nGreen\nCyan\nAqua\nNight\nBlue\nDenim\nLapis\nSapphire\nGreenish\nCharcoal\nTurquoise\nIridium\nGreen\nLimegreen\nJellyfish\nTaupe\nMocha\nIndigo\nCeleste\nIceberg\nGrape\nEggplant\nCoffee\nGray\nSandstone\nSangria\nSepia\nPuce\nAquamarine\nFirebrick\nMaroon\nMoccasin\nGranite\nSienna\nChartreuse\nBurgundy\nViolet\nPurple\nChestnut\nWood\nCranberry\nKhaki\nBrass\nCopper\nPlum\nMahogany\nSand\nRust\nCinnamon\nCaramel\nChocolate\nLilac\nSedona\nBronze\nThistle\nGrapefruit\nBurlyWood\nMauve\nCrimson\nPlatinum\nTangerine\nRose\nPeriwinkle\nWater\nGoldenrod\nAliceBlue\nAzure\nVanilla\nBeige\nRuby\nFire\nViolet\nChampagne\nFlamingo\nBeer\nSaffron\nBlonde\nPearl\nRed\nMagenta\nScarlet\nCoral\nMango\nOrange\nCantaloupe\nMustard\nPink\nPeach\nSeaShell\nLemon\nYellow\nCream\nWhite",
    /*  1 */ "Ativan®\nlorazepam\nCandy\nDowners\nSleeping Pills\nTranks\nHalcion®\ntriazolam\nCandy\nDowners\nSleeping Pills\nTranks\nKlonopin®\nclonazepam\nK\nK-Pin\nPin\nSuper Valium\nLibrium®\nchlordiazepoxide\nCandy\nDowners\nSleeping Pills\nTranks\nRohypnol®\nflunitrazepam\nCircles\nDate Rape Drug\nForget-Me Pill\nLa Rocha\nLunch Money\nMexican Valium\nMind Eraser\nRoofies\nWolfies\nValium®\ndiazepam\nEggs\nJellies\nMoggies\nVallies\nXanax®\nalprazolam\nBars\nBicycle Handle Bars\nFootballs\nFrench Fries\nHulk\nLadders\nSchool Bus\nXan\nXanies\nZan\nZannies\nZanbars\nZ-Bars\nAyahuasca\nAya\nHoasca\nYagé\nDMT (N,N-dimethyltryptamine)\nDimitri\nThe Spirit Molecule\nGHB (gamma-Hydroxybutyric)\nDate Rape Drug\nG\nGeeb\nGeorgia Home Boy\nGina\nGoop\nGrievous Bodily Harm\nLiquid E\nLiquid X\nScoop\nKetine\nBlind Squid\nCat Valium\nGreen\nJet\nK\nK-Hold\nKay\nSpecial K\nSuper Acid\nVitamin K\nKhat\nAbyssinian Tea\nAfrican Salad\nCatha\nChat\nKat\nOat\nQat\nKratom\nBiak-biak\nHerbal Speedball\nIthang\nKahyam\nKetum\nThom\nLSD (D-lysergic acid diethylamide)\nAcid\nBlotter\nDots\nElectric Kool Aid\nLucy in the Sky with Diamonds\nPurple Haze\nSugar Cubes\nYellow Sunshine\nMescaline\nBig Chief\nBlue Caps\nButtons\nCactus\nMescal\nMoon\nSan Pedro\nTopi\nMushrooms\nAlice\nBoomers\nCaps\nCow Patties\nFungus\nHongos\nMagic\nMagic Mushrooms\nMushies\nPizza Toppings\nShrooms\nTweezes\nPCP (phencyclidine)\nAngel\nAngel Dust\nButt Naked\nDust\nPurple Rain\nRocket Fuel\nStardust\nWater\nWet\nYellow Fever\nZombie\nPeyote\nBlack Button\nCactus\nGreen Button\nHalf Moon\nNubs\nShaman\nTops\nPsilocybin\nLittle Smoke\nMagic Mushrooms\nPurple Passion\nShrooms\nSalvia divinorum\nDiviner’s Sage\nMagic Mint\nMaria Pastora\nSally-D\nSynthetic Cathinones\nBath Salts\nBliss\nCloud Nine\nFlakka\nLunar Wave\nScarface\nVanilla Sky\nWhite Lightning\nBlack Tar Heroin\nChiva\nMexican Black Tar Heroin\nMexican Tar\nCocaine\nAunt\nAunt Nora\nBatman\nBazulco\nBernice\nBernie\nBig C\nBig Rush\nBlow\nBlowcaine\nBump\nC\nCandy\nCharlie\nCoca\nCoke\nColombia\nCrack\nDust\nFlake\nGringa\nHubba\nLine\nPearl\nPowder\nRail\nRock\nShe\nSnow\nStardust\nStash\nWhite Girl\nCocaine with Heroin\nSpeedball\nCrack Cocaine\nApple Jacks\nBase\nBall\nBlack Rock\nBlotter\nBopper\nChemical\nCrack\nCrankenstein\nDice\nGarbage\nGrit\nHail\nHard Rock\nKryptonite\nPurple Caps\nNuggets\nPoor Man’s Cocaine\nRedneck Cocaine\nRock\nRocks\nSleet\nSugar Block\nTopo\nTornado\nTrash\nTrey\nYam\nYay\nHeroin\nBlack\nBlack Tar\nBlack Pearl\nBlack Stuff\nBlack Eagle\nBoy\nBrown\nBrown Crystal\nBrown Rhine\nBrown Sugar\nBrown Tape\nChina White\nDope\nDope\nDragon\nThe Dragon\nH\nHe\nHorse\nJunk\nMexican Brown\nMexican Mud\nMexican Horse\nMud\nNumber 3\nNumber 4\nNumber 8\nSack\nScat\nSkag\nSkunk\nSmack\nSnow\nSnowball\nTar\nWhite\nWhite Nurse\nWhite Lady\nWhite Horse\nWhite Girl\nWhite Boy\nWhite Stuff\nMarijuana\n420\nAshes\nAtshitshi\nAunt Mary\nBammy\nBaby Bhang\nBlanket\nBlunt\nBo-Bo\nBobo Bush\nBomber\nBoom\nBud\nBroccoli\nCheeba\nChronic\nCripple\nDagga\nDope\nDinkie Dow\nDing\nDona Juana\nDona Juanita\nFlower\nFlower Power\nFlower Tops\nGanja\nGasper\nGiggle Smoke\nGiggle Weed\nGood Giggles\nGood Butt\nGrass\nGreen\nHash\nHerb\nHot Stick\nJane\nJay\nJolly Green\nJolly Green Giant\nJoy Smoke\nJoy Stick\nMJ\nMaryjane\nMary Jane\nPot\nRoach\nReefer\nSkunk\nSmoke\nTrees\nWeed\nMDMA (Ecstasy/Molly)\nAdam\nBeans\nCandy\nClarity\nDancing Shoes\nDisco Biscuits\nE\nE-Bomb\nEgg Rolls\nEve\nHappy Pills\nLover’s Speed\nMalcolm\nMalcolm X\nPeace\nRolls\nScooby Snacks\nSmartees\nThe Vowel\nTriple Stacks\nUppers\nVitamin E\nVitamin X\nVowel\nX\nXTC\nMethamphetamine\nChalk\nChristina\nCookies\nCotton Candy\nCrank\nCream\nCrystal\nCrystal Meth\nDunk\nFire\nGak\nGarbage\nGlass\nGo Fast\nGo-Go Juice\nIce\nMeth\nNo Doze\nNo Stop\nPookie\nRocket Fuel\nScooby Snacks\nSpeed\nTrash\nTweek\nWash\nWhite Cross\nSynthetic Marijuana\nBlack Mamba\nBombay Blue\nFake Weed\nGenie\nK2\nMoon Rocks\nSpice\nZohai\nAmyl Nitrate\nAmes\nAmies\nAmys\nPearls\nPoppers\nIsobutyl Nitrate\nAroma of Men\nBolt\nBullet\nClimax\nHardware\nLocker Room\nPoppers\nQuicksilver\nRush\nSnappers\nThrust\nNitrous Oxide\nBuzz Bomb\nHippie Crack\nLaughing Gas\nWhippets\nBuprenorphine\nSuboxone®\nSubutex®\nBig Whites\nBuse\nOranges\nSmall Whites\nSobos\nStops\nStrips\nSub\nSubs\nCodeine\nCaptain Cody\nCody\nLittle C\nSchoolboy\nCodeine with Promethazine\nPhenergan®-Codeine Syrup\nAct\nLean\nPurple Drank\nSizzurp\nTexas Tea\nFentanyl\nActiq®\nSublimaze®\nApache\nChina Girl\nChina White\nDance Fever\nFriend\nGoodfella\nJackpot\nMurder 8\nTango and Cash\nTNT\nHydrocodone\nLortab®\nNorco®\nVicodin®\nBananas\nDro\nFluff\nHydros\nTabs\nVikes\nV-itamin\nWatson-387\n357s\nHydromorphone\nDilaudid®\nExalgo®\nD\nDillies\nFootballs\nJuice\nSmack\nMeperidine\nDemerol®\nDemmies\nPain Killer\nMethadone\nDolophine®\nMethadose®\nAmidone\nDollies\nDolls\nFizzies\nMud\nRed Rock\nTootsie Roll\nMorphine\nDuramorph®\nMS Contin®\nGod’s Drug\nM\nMiss Emma\nMonkey\nMorpho\nWhite Stuff\nOxycodone\nOxyContin®\n30s\nAs\nBerries\nBlues\nBlueberries\nHillbilly Heroin\nMs\nO.C.\nOxy\nOxycet\nOxycotton\nOzone\nRoxy\nPercocet®\nErcs\nGreenies\nKickers\nM-30s\nPercs\nRims\nTires\nWheels\n512s\nOxymorphone\nOpana®\nBiscuits\nBlue Heaven\nMrs. O\nO Bomb\nOctagons\nStop Signs\nPropoxyphene\nDarvocet®\nDarvon®\nFootballs\nN’s\nPink Footballs\nPinks\nYellow Footballs\n65s\nTramadol\nUltram®\nChill Pills\nTrammies\nUltras\nDimenhydrinate (Dramamine®)\nDime\nDime Tabs\nSubstance D\nDXM (dextromethorphan)\nDex\nDextro\nDrix\nPoor Man’s Ecstasy\nRed Devils\nRobo\nRobotripping\nTriple C\nTussin\nX\nPseudoephedrine (Sudafed®)\nChalk\nCrank\nMeth\nSpeed\nAmbien®\nLunesta®\nSonata®\nDate Rape Drug\nForget-Me Pill\nMexican Valium\nR2\nRoche\nRoofies\nRoofinol\nRope\nRophies\nAmytal®\nBarbs\nRed Birds\nReds\nYellows\nYellow Jacke",
    /*  2 */ "Mary\nPatricia\nJennifer\nLinda\nElizabeth\nBarbara\nSusan\nJessica\nSarah\nKaren\nNancy\nLisa\nMargaret\nBetty\nSandra\nAshley\nDorothy\nKimberly\nEmily\nDonna\nMichelle\nCarol\nAmanda\nMelissa\nDeborah\nStephanie\nRebecca\nLaura\nSharon\nCynthia\nKathleen\nAmy\nShirley\nAngela\nHelen\nAnna\nBrenda\nPamela\nNicole\nSamantha\nKatherine\nEmma\nRuth\nChristine\nCatherine\nDebra\nRachel\nCarolyn\nJanet\nVirginia\nMaria\nHeather\nDiane\nJulie\nJoyce\nVictoria\nKelly\nChristina\nLauren\nJoan\nEvelyn\nOlivia\nJudith\nMegan\nCheryl\nMartha\nAndrea\nFrances\nHannah\nJacqueline\nAnn\nGloria\nJean\nKathryn\nAlice\nTeresa\nSara\nJanice\nDoris\nMadison\nJulia\nGrace\nJudy\nAbigail\nMarie\nDenise\nBeverly\nAmber\nTheresa\nMarilyn\nDanielle\nDiana\nBrittany\nNatalie\nSophia\nRose\nIsabella\nAlexisKayla\nCharlotte",
    /*  3 */ "James\nJohn\nRobert\nMichael\nWilliam\nDavid\nRichard\nJoseph\nThomas\nCharles\nChristopher\nDaniel\nMatthew\nAnthony\nDonald\nMark\nPaul\nSteven\nAndrew\nKenneth\nJoshua\nKevin\nBrian\nGeorge\nEdward\nRonald\nTimothy\nJason\nJeffrey\nRyan\nJacob\nGary\nNicholas\nEric\nJonathan\nStephen\nLarry\nJustin\nScott\nBrandon\nBenjamin\nSamuel\nFrank\nGregory\nRaymond\nAlexander\nPatrick\nJack\nDennis\nJerry\nTyler\nAaron\nJose\nHenry\nAdam\nDouglas\nNathan\nPeter\nZachary\nKyle\nWalter\nHarold\nJeremy\nEthan\nCarl\nKeith\nRoger\nGerald\nChristian\nTerry\nSean\nArthur\nAustin\nNoah\nLawrence\nJesse\nJoe\nBryan\nBilly\nJordan\nAlbert\nDylan\nBruce\nWillie\nGabriel\nAlan\nJuan\nLogan\nWayne\nRalph\nRoy\nEugene\nRandy\nVincent\nRussell\nLouis\nPhilip\nBobby\nJohnny\nBradley",
    /*  4 */ "Scalable\nMarkets\nManagement\nOff-line\nSales\nPenetration\nBenchmark\nProactive\nFree\nCloud\nOutsourcing\nCustomer Value\nShareholder\nParadigm\nProfit\nStrategy\nDisruptive\nSchedule\nCost\nReview\nGranular\nFacilitate\nBlock chain\nNFT\nCryto\nHyper\nInternet\nOnline\nHighspeed\nCustomer\nEngagement\nDeep\nAgile\nLeverage\nDiversity\nWin-WinValue\nMobile\nSlim\nFast\nSocial\n",
    /*  5 */ "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.",
    /*  6 */ "; memories by HellMood/DESiRE\norg 100h\ns:\n%ifdef freedos\n        mov fs,ax\n        mov [fs:0x46c],ax\n%endif\n        mov al,0x13\n        int 0x10\n        xchg bp,ax\n        mov bp,512*4\n        push 0xa000-10\n        pop es\n%ifndef freedos\n        mov ax,0x251c\n        %ifdef safe_dx\n                mov dx,timer\n        %else\n                mov dl,timer\n        %endif\n        int 0x21\n%endif\ntop:\n%ifdef freedos\n        mov bp,[fs:0x46c]\n%endif\n        mov ax,0xcccd\n        mul di\n        add al,ah\n        xor ah,ah\n        add ax,bp\n        shr ax,9\n        and al,15\n        xchg bx,ax\n        mov bh,1\n        mov bl,[byte bx+table]\n        call bx\n        stosb\n        inc di\n        inc di\n        jnz top\n        mov al,tempo\n        out 40h,al\n        in al,0x60\n        dec al\n        jnz top\nsounds:\n        db 0xc3\n%ifdef music\n        db instrument,0x93\n        %ifdef switch_uart\n                db volume\n                db 0x3f\n        %endif\n%endif\ntable:\n        db fx2-s,fx1-s,fx0-s,fx3-s,fx4-s,fx5-s,fx6-s,sounds-s,stop-s\nstop:\n        pop ax\n        ret\ntimer:\n%ifndef freedos\n        %ifdef safe_segment\n                push cs\n                pop ds\n        %endif\n                inc bp\n        %ifdef music\n                test bp, time_mask\n                jnz nomuse\n                mov dx,0x330\n                mov si,sounds\n                outsb\n                outsb\n                outsb\n                imul ax,bp,scale_mod\n                shr ax,10\n                add al,22\n                out dx,al\n                outsb\n                %ifdef switch_uart\n                        inc dx\n                        outsb\n                %endif\n        %endif\nnomuse:\n        iret\n%endif\nfx0:\n        mov ax,0x1329\n        add dh,al\n        div dh\n        xchg dx,ax\n        imul dl\n        sub dx,bp\n        xor ah,dl\n        mov al,ah\n        and al,4+8+16\nret\nfx2:\n        xchg dx,ax\n        sub ax,bp\n        xor al,ah\n        or al,0xDB\n        add al,13h\nret\nfx1:\n        mov al,dh\n        sub al,100\n        imul al\n        xchg dx,ax\n        imul al\n        add dh,ah\n        mov al,dh\n        add ax,bp\n        and al,8+16\nret\nfx3:\n        mov cx,bp\n        mov bx,-16\nfx3L:\n        add cx,di\n        mov ax,819\n        imul cx\n        ror dx,1\n        inc bx\n        ja fx3L\n        lea ax,[bx+31]\nret\nfx4:\n        lea cx,[bp-2048]\n        sal cx,3\n        movzx ax,dh\n        movsx dx,dl\n        mov bx,ax\n        imul bx,cx\n        add bh,dl\n        imul dx,cx\n        sub al,dh\n        and al,bh\n        and al,252\n        salc\n        jnz fx4q\n        mov al,0x2A\n        fx4q:\nret\nfx5:\n        mov cl,-9\n        fx5L:\n        push dx\n                mov al,dh\n                sub al,100\n                imul cl\n                xchg ax,dx\n                add al,cl\n                imul cl\n                mov al,dh\n                xor al,ah\n                add al,4\n                test al,-8\n        pop dx\n        loopz fx5L\n        sub cx,bp\n        xor al,cl\n        aam 6\n        add al,20\nret\nfx6:\n        sub dh,120\n        js fx6q\n        mov [bx+si],dx\n        fild word [bx+si]\n        fidivr dword [bx+si]\n        fstp dword [bx+si-1]\n        mov ax,[bx+si]\n        add ax,bp\n        and al,128\n        dec ax\nfx6q:\nret\n",
    /*  7 */ "In the beginning God created the heavens and the earth.\nNow the earth was formless and empty,\ndarkness was over the surface of the deep,\nand the Spirit of God was hovering over the waters.\nAnd God said, “Let there be light,” and there was light.\nGod saw that the light was good, \nand he separated the light from the darkness.",
    /*  8 */ "Adapt\nAlcatraz\nAltair \nAndromeda\nASD\nArchee\nBauknecht\nBlack Maiden\nBrain Control\nBrainstorm\nCNCD\nCTRL+ALT+Test\nCLRSRC\nCocoon\nConspiracy\nDeranged\nDesire\nEinklang\nElude\nElyssis\nEphidrena\nExcess\nFarbrausch\nFairlight\nHaujobb\nHolon\nLoonies\nKakiarts\nMercury\nMFX\nNeuro\nNoSYS Productions\nNuance\nPlastic\nPoo-Brain \nPortal Process\nPrismbeings \nOutracks\nQuite\nRabenauge\nRebels\nRGBA\nScarab\nSpacepigs\nStravaganza\nSynesthetics\nTBC\nThe Black Lotus\nThe Electronic Knights \nTpolm\nTraction\n",
    /*  9 */ "0\n0.125\n0.25\n0.5\n1\n1\n4\n8\n16\n32",
    /* 10 */ "3\n5\n7\n11\n13\n17\n19\n23\n29\n31\n41\n43\n47\n53\n61\n71\n73\n79\n83\n89\n97\n107\n109\n113\n127\n137\n139\n151\n163\n167\n173\n179\n181\n191\n193\n197\n199\n211\n223\n227\n229\n239\n241\n251\n269\n277\n281",
    /* 11 */ "2\n23\n37\n47\n53\n67\n79\n83\n89\n97\n113\n127\n131\n157\n163\n167\n173\n211\n223\n233\n251\n257\n263\n277\n293\n307\n317\n331\n337\n353\n359\n367\n373\n379\n383\n389\n397\n401\n409\n439\n443\n449\n457\n467\n479\n487\n491\n499\n503\n509\n541\n547\n557\n563\n577\n587\n593\n607\n613\n631\n647\n653\n673\n677\n683\n691\n701\n709\n719\n727\n733\n739\n743\n751\n757\n761\n769\n773\n787\n797\n839\n853\n863\n877\n887\n907\n911\n919\n929\n937\n941\n947\n953\n967\n971\n977\n983\n991\n997",
    /* 12 */ "7\n13\n19\n23\n31\n79\n97\n103\n109\n139\n167\n193\n239\n263\n293\n313\n331\n367\n379\n383\n397\n409\n487\n563\n617\n653\n673\n683\n709\n739\n761\n863\n881\n907\n937\n1009\n1033\n1039\n1093",
    /* 13 */ "2\n3\n5\n13\n89\n233\n1597\n28657\n514229\n433494437\n2971215073\n99194853094755497\n1066340417491710595814572169\n19134702400093278081449423917",
    /* 14 */ "5\n53\n157\n173\n211\n257\n263\n373\n563\n593\n607\n653\n733\n947\n977\n1103\n1123\n1187\n1223\n1367\n1511\n1747\n1753\n1907\n2287\n2417\n2677\n2903\n2963\n3307\n3313\n3637\n3733\n4013\n4409\n4457\n4597\n4657\n4691\n4993\n5107\n5113\n5303\n5387\n5393",};
static const int kMockStringsCount = (int)(sizeof(kMockStrings) / sizeof(kMockStrings[0]));

// Euclidean mod (MathUtils.Mod 1:1): non-negative result; 0 when repeat==0 (fork-…-euclidean-mod).
int euclideanMod(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

// MockStrings: Category (enum dissolved to Float) → host string. Implements MockStrings.cs Update() 1:1.
void cookMockStrings(StringCookCtx& c) {
  if (!c.output) return;

  // Category: resolved Float param truncated to int (enum index). No String inputs are read.
  const int category = (int)stringFloatParam(c.params, "Category", 0.0f);
  const int idx = euclideanMod(category, kMockStringsCount);  // wraps into [0, count) for any int
  *c.output = std::string(kMockStrings[idx]);

  // Test-only: corrupt the REAL output (drop the last char) so the golden's RED case fires on the
  // actual cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Port ORDER:
//     [0] "Result"   = String output (the host string currency — String PRODUCER)
//     [1] "Category" = Float/enum input (value spine; enum dissolved to Float; default 0 = Colors)
//   NO String input ports → inputStrings is empty; Category rides params (Widget::Enum, 15 labels).
static const StringOp _reg_mockstrings{
    {"MockStrings", "MockStrings",
     {{"Result",   "Result",   "String", false},
      {"Category", "Category", "Float",  true, 0.0f, 0.0f, 14.0f, Widget::Enum,
       {"Colors", "Drugs", "Females", "Males", "Bullshit", "LoremIpsum", "Assembler", "Preaching",
        "Demogroups", "ValuesToRates", "RegularPrimes", "IsolatedPrimes", "HappyPrimes",
        "FibonacciPrimes", "BalancedPrimes"}}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookMockStrings};

}  // namespace sw
