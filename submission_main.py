"""Kaggriculture v20: mirror control plus public-state slip recovery.

Research lineage and falsifiable validation are documented in the public v20
Notebook.  The runtime uses public farms, public money, own private inventory,
and the current market only.  It has no network access or team-name lookup.

The bounded WEED transaction is inspired by prvsiyan's public "The Moon Counts
Melons" Notebook and independently generalized for the frozen v20 route.
"""

import base64
import copy
import json
import math
import zlib


_ACTIONS = json.loads(zlib.decompress(base64.b85decode(
    (
    'c-'
    'rk<O>Z1oa{Mnm^B^`!t;RR5)awyeGZG}t5^I4NEZ{W^80*8>H^cwk(vaQNRT&u(neVkmyYOjxTI{O#{W2pXBR~Dm#lQXKm%sh>my'
    '3V;bn*M2UcY(u^SiqbAAfqkzj(O3`1im3=fD2f=YRS9@o#_m<v;%V-'
    '=9B!`LjR&eD~w)AMV~<Twc6<dw+3x{cySY`os78{kx0HtHVEh*zaF`{`!ago3}q+T>ftJ_5JtzyN{p$`q|<8ckkc6`swAzlYjd4C'
    '*QyRwOxk~5C404+Wa5izW@0BX|q3F-0wep{PhP<|F-Uk`;^01pDy0Le)-3r-W|GqarNt$k1y#xdOPHwU-'
    '9Pd<^JgnCoPBX**yRAPk&s-Ea}2=NRA(3r^q|*?>_F|t3JeU=1j!#Df_!a+c#Z~<9F=c(@GNkJ3R1lrLNu%-'
    'UXh0xs1`Liw|#q+OC|Zolzg=<*CbLij@fld;6Zp5rr$|4_`JX9%MG1^#MKn)2EA<cZc<I>};6Nr~iK($A@?{#k1p~GPui>p<#X;l'
    'hT03eYRdLaTJcfcpQ#S7s;qET|G`PI1}zZd?+u_b+fbGx&0=*$)3hI)XS<#cIL68%U`OVlA&DI$}({IUg?Z!eB3f=r&~l0YGLe_>'
    'HGC5Vzef#$m4tBhm!-eGkW=h8)NqLTkrXm1y*_P=EHCB*zL}`;g;gY#M5Tn(*ifGIBhf}N5RXtZ{F-'
    '*e*F0l`}ZGTzj^&HUuKIuIJWAzO<Jv54$IMY91nzeTWo%dUbX5G`uAr4j-w-'
    '61@<koc{^r%VF8(jm$b!+9k4u4KZUjCXaqlf_1m^)`@m*v)*nVM%ai7HS`|C@bRX5)bz&wI+H?37d{E(D1kX9HoNwzfi&v+f^7NN'
    'u52@T_n=9)}pZ9OR039c1<>k%TRfdp~s}vErc{tnE=bCh+d&76p@m5O*O1BSPOZbW_I9h$P#W~f~uz+Xwoy4a*ntJ5H2`-'
    'vB+XJtA2_pb&>_{KF_s-ay))fOT(BE)$kY7CFkrtdO+M2O@3c`ASU$^g+d-EUi^8NqI)V=8ZTb14&{c+hk-ZMrBVZC!LkCPi?gR~'
    'jmt><~2$31O6E_DpMo@RVZ@T*2!ctpW^IKfwF;gEk!{LuLx>GVr>_BA<p)lGif=H~nsp7!ef+jsWU;GG<U0JCrX>#s%v$f9>F&{Q'
    ')pP~^21yas!IL5FQ{w`>uhZ5(2O$@>w&J8UpzrrO<#jWESrh$r)0vw=lx^)2=N-'
    'TOa<Yi0Q|pA~j148&){syJ=S@pB4_efaqP?*8}t_wWB4sYR_|zjLpmx9lF|>3P~S6n0)?)yA20_{GT}NCWe<5rNoX4)fS}MoivDK'
    'K15Ujd5<$SW&33SYylKV)a~xkwv;ZejD*Atk>-9W_*>Dy|KW9ON)k>BXsUqSf>Fi(LDhkK8!l$<G9+&rb%h-'
    '6%*PUvbAZSftlQ}Nw?il2IAIIJcLS~xtI|+1q6{O-SKON>{ZT?-'
    '83O3;R>^`ZY_3z97SUs3C%=UrxgUoW3#cIx|Vq|KxZN)26L$a#6&J`IO@8ad@wcym~0^S;Ui-4E}{;>9Fta3fB`45$+OP+EG6F-'
    'cxzxLIw&Ba_$B)W-3}YR?<{;B4CrxNAz+Nqs@zvnyex9rkIi}fW>P1QC{7+uyFC1r1at!20v$}i-'
    'kb#HfhRAQ;*<x+*%*P);$s_(%QHPToLtWvSlrWZuM-37YLpSvM(1|FJ?ONX9K1Ud!bHbt==_U=1t3mMcYtS%!ZqzF-'
    'c``d&Y%=f)8lqV;YfM?KArR;s#zGX!ybS8_Va(9yIVZ+&VeoA(}UyVmcB6Cc95<FK$lK76+mn_<k}1nZbRV{XjapMm;e))6=$8qd'
    'TQ~O%s8w4Kr_lZixUOrr;%EMS~x%kdd_o7KKmK3E&`&m04-MN@K#?}$(3H^?gCi-'
    '21hS$aO=zYu`C<(e8e0?PHi!RV1L`x*!f+L#Mx-'
    '1CBVQ5l$LRAr$Bc1B=*G9YldlAU?M`k_6GUZJy)|I!Jp%lpr}bds!8VFfi7S?fSDNcEqky(Vz6a1LH>Xlm^yeZG<fIm#b9Jd1z<X'
    'MMBZ{0&YGGAX0P}mN?<aATY#q9DNa=@yoGslhSB2nge9VbOy(r@*s+x2u?vtjI%M({@qhzQsnOsD0Mj@hO#5RBQ82!#GmM0tw?SP'
    '^_zoJel88bAlZi%8AlZ;Os}8}^a4WQtJXZE)m62B=y<8W9vQpkd*@4TlkU^@l9{?`FqqP#j6P6~>VyeYgVA{;-x6KIiu1c5EWS-'
    '?{i~Tt6T)3>B*8UH#-~90la8`j`q8+<^Rc=^jaii^y-(}P}uz#~YkSp)lV8vGev6gil-'
    'C7QthPpfpK_^4+z0}@TW)|V6o&Ic;KR{^<aaBp;r{+9ZG4(E`FhoaJtkrd-'
    '6fUKqoRu7ZcbtdqxYvx})hZ18fw~rP6grr7<^o2u84Va{^4cSxzeXTDDrU<G>5S`5QDTMA1A`47r|KlT8M7i$`|JA(ATjK=wetJA'
    'wA!TmOzV!R`K7EMHA=t%*~G+FFNADzKzQopa7DkwQU0@*hBZqx3+&h?d0|ETu>$ANsLRp_z!(a48q3EvL#VlsCumf>1fyR$kRxtW'
    'Tfs4Y+xH(!Szmx6bJ(G$zgx#|xHGw9UyCThmK@`^)_S|!sM-_Ak|6Fcu#dKG<mp$NtDs331#j@p)zgqzW|0JQ`+7iIhDpv~^KG%?'
    'dgRCJ3i<IqzgObLZD*`os01)!scY*bYK<|i*&+^Js)+MQgOa*zuU`MN<#EC-kUK$X)TWRi#8@XX#^T%K*4kGVV9v-'
    'nK3A?u>T`AU4FS(<(;Axmm2B7&7|qU7613XP8Zi_{U0)fzJjEOF*vUsU#ti@sadi+_qM%t~2FO_akB8f9^ujn66cM2NXjYI{It3s'
    'm1~wnim9M2z#j!a~_<C^ygWQe1gHE(F5{b?-YFO%YG+Y}(9b?U+V9&;JZG{U$yGU@3k5TleCAs#-'
    'j+j+Mok5TmtaR2$bL_%q*;$&<RyzM#1CV|u!8D(-'
    'YY*057DF#9KlrNS1r`I1r<Xa&z8r{eL>oqB1)Na?MVK>&X%nLw4c)eDgML*Sn`p~KuH)2%wFrX(H`D_IJ1v6QCcsos!xd+9fZPYr'
    'Ntz96j~JS{vc?ZO8`X>BEYHf(rTIISyaYpe^(ju_Y=GUTv|JI#OD0Mv(Iie2^hk3UPme-OM#|%(myJv%A<Dkc0v@^e&lSn3YbDZH'
    '8L|_VAXb{wl%+vb0vXVXQ9dHw=L3ftL6vg?tNi5YY|42BE0X{)_vo4jtfHEAMN_xwnTDJs%2YbpB`ZEnp4gl3r(KDff&<UYu*}2#'
    'UE4a<RZ3RY{1%|8Tc%&w961(5m{NAiHi1y_+=UuTo0H{u0v<bf{E$e%tyk&Q&Q3+GI;QK$=8i1#=My5EgURh8$*UPt^dCm&=tVL|'
    'FTfE~*(kDqP=bzEgxnQPEK-'
    '9B*h(>^lr&(f+t*4565JT|@9GazEW+sEt<gb7FM{+zWs8w4b$1>f!+2J<Sa29wB3&Vfp_tHqgpon#X~47w|5YNBUivy9Ci{u1kl`'
    'pD)hL|cS;Qd3!lX4732h@`)Fp^|!)$wNGQywnAv!(=8&MFw20mO8$Vu6^{fH-'
    '$$q<7|18?kz=#L*m3exbn5tB5MbCu|&l_japd>RIi@QvO-'
    '9FXz7@3@<#4?ngxdFHs<_&kG?Y7knp4M=o(A!S860f?U!5$#6r3z1$XHJ<COR^^ny>B|RzHD-'
    '(kO{TGsz^!H+`mjv`GfjqbQ_a;lr@`EEb1il;4QwMoF*@^HX2MBk^+*h$fwB+=#QmcPqQn!t;>S;C*e$^muBXSp2hjyI$1`clRW0'
    'Y!h4LNHcrMA~gd?U9^;d2@feCwcoq<6gKM)ENi2U0kaMgMeCs{l?&q(N_EVCoXxTk+Tzx;J<PNYuzGH-x(-'
    'B9MB3Mv+dxlvJy2tJ8)O@SN9us$>6W)xFRE2w3qN}?_9>*h4Hq-d~M`O__~r4JvnggMSlCz)HtiMG-'
    'k(rh^u>>MnSMs6K&vV&=JHPo8+Hg`I7fuKAoyE1U`a9MfDk;cn|+Nv1NM8Pp87vnIV+ogGSTAB<<&OD`+i1I`{GFjZ7R?cYw%phB'
    'urhE?9k|H(oPAJGY8Nrk3qL8N!)=Lsjbc4)P6Y8X@^>9wMGsqiv84+-*N-'
    'Pn_tK_tSoMJYTKU^9&s?f`9q=rsxKl6aeUDO3;YmWQ%nNyr+5dktsbaHyJPX<^tdBC!L9=&*jd{UqSu>50|AKvhUvIOU0PC86#al'
    'G={+hij{$b8J`jOMxW{ir|VU`l>En;Zj|`b~+^8WW1wH=70!#AFmf)()lq#WeXD`e2kzl>w#FDW6QRS3E+RJws>|%V+UJli<eInZ'
    '(-CDX1Gnqbq9w4J3@fP)vE+Pl`ntp3jm{?jm15_g)oKWP=qUvEU-'
    '|5}4aEEq3PdpyCb++{;{F<u?+iwh8Gl!UHo{6XCSVOvcNBy^z8WWjA`X3=HQ6$f&*J-'
    'j%!(HgePh;U1lT8L+{kq4^R$zUkz~U@CeA2>a5O%u66=?PE9-'
    'SnUg(lTY2(X=dD$AY;3J##U>V%nCziRyg$Bz7>y`mUc^g4YW2&Uabl*vusb4jMh-qh(0WgeSz0vuzi+_6$|Mf8xwO$GPGO9s;Rb{'
    ';Pzg(%hx~^;!Hf;W!d!^pjOsDurnChy0$n%&B>>s*Wu~N{Fd^cic?JmOUumg;u^s^<qbw_Ou^#CB`rL!kVi{xe?Op2^Y-'
    '=*;kPG7Oag6;N)CXTVrs3Qn&<Na6a_tzW9SIQ#4U>2iwGCoOEqN&hV(rk$O<xaS&j@nJgnS|G_Tz#TqO<{QGzL+rSKNA<=cq;pPi'
    '0(M0^w)9uAo!anJ`;!Fj%vr!miR4=NmiW|D*_E`kR=2XSgD^sJhZ4-dIvPEEp#0~eh&hR_C#cnJ|p+WOX0>{SaNkx-'
    'CW%$z1NCpmCfJiff1+V+r<;$92MON+1dnkKL(E6yLq%e!@_x)Jv20K;ONuIsJ-<3-n3w)ZnC-ErXEbV|53g6o~~u9kD}>u`0G-'
    'L=t8g8z1cJX4YNOQV<K{=!Lw2oC@#!MbQ<^IsQ=gxZ;#1q;B=f0$4VnTP>{v_s$r>4=6h?FlJ`JJUJr(GvS}7pu)ip&4nM;oy0!W'
    'L>JZxju)kpVArGR6<U99<87e!`{pl#;e!15|hLX5b)BftTQ4==oUWq=m0I+KS?fOi=k6=i0WE!ioE$rpOLW*l*r&J+YXHax6K*tB'
    'hm&~Iq7CXy_*hT!Yn7epc1bk5O<Jo#a8Otwuy4=Cf1!8(InD~1}nKXjt&?NvYX!7mam*mxg~pu;vAuXaT>~=8aekCfX8(|8)y6+9'
    '<3=urd&>13+pSmv8_-'
    '#mn*y}smHNI&l*Sk^h_y;!B}bDAdIhIPO29pLLmcQB{+5Az@)74f$B*zog28;Jx)A2K2kNF8ml%7ICQni++5MoB?{0Vq?%35+;LK'
    '^qHh-'
    '><E}yOx@gxrdc`UTv47*XGn7#E<Hk(a$Edxuue)v}D9i?i1<f!|!pOx}933y6*ZT^MT*tj+VNtWZZZe5^_2Tgib9~$3!~tLbxIc='
    't!h)c*;@EQpbSvRT&t>N2hw|dR6_8BiL`Q!Ojh2`FT=wxzBk@}M7)dz<;Ef%kkCWusEb6zx&{wO$=35WDj<F1}n~?2M2iiH%!F!C'
    '9YT!kh2~1jsmrOLMH5N86V6f8$MKv=R8Qt~w40E|@kQ|*0`*Y`VpZewNJ7FEzK9jc&7%ropHE>Hu3GUlRDwVbrETcjtBm8$&@QmN'
    'plN=#R@$%r7l2V#AKUJ+ZM??<zz@}3*U}j5q7WTqV6m*LXO`^+l<?v!Jfiant92=s$5|L0!64qVG3-'
    'pXeZ;h}h98($(C8X`O%~(l6q{5~Oj<O}y>G+OPAa)hu5_ACPQJEMEp$KlCRjUF!?HYytX=vu+Lejg({SCL)GL(?1UMuUs7|GRvkG'
    'fS!NbTOyr;c|N^(dJ<U7;v9L95n_c>RBJb*O6e_ynDi;Dzyi#$us*3q>V^0bYeNBY?w1@i31h0_t!mk0l~>o1)87*pRyY$&uklIu'
    'REZ%h^RAAFYoALc-zojnCMm-e)@qU=P%{Pj)eBq>Vj!fC`%g=2aIt=y<U>-K;9LlX~PuH99(la`m7R1KoiomDSJIv~<UC-'
    '2f1p9cY-X);^7~|6;aOz~2}7L9-'
    'wpZKtc}{W^{Xjz3ESW0?R2!_^WZVDCbE(J8xLDOR_erd$9$(Imm!cJHdnbYH8oAWQQtW^4=4y-'
    '(PWncJo+ILoi6%FeT4F)DSfsjQxEx67O-y7r{fA(f(z9v^~YYKE>D04X98Gw4hppa!vADoIALo--H;8uT>tnDJBgRvo2r?iM^R8J'
    'HpK3}zaNb4VB<b}Q45xDr3}Q$Yguq&$+uzp@T;rgW8izT>qhDKARKqYYtz61R$Ef@!Tzn&H$c?Lid)QVNclrBNb_b)^MO<w)KZwm'
    '$*fLeYKCN2blD8Rx96j2bdVIHJkqloLxkL$1+*8zV9(%=~yttMHQHOeAQhfp3BoPVE;G+Z>Q0h;U}LX#{7T+c2gp5z|`0CFLk3td'
    '$2$|GdO&`5Fk!)vi)={Tq;E99C+)F67nvWdJEPCC}}vNnq=^d-'
    'R?dG{!U~^#l5uy2tJ1i=j)Dpa%S8RqFvV;=5;*7Sffl@YJ0%Z+nu=f#s*uBoU@)*pk4Aa91A&G_x*vty>RR1NLDJ4;=YRF!=f$Vu'
    'igr`jobdTL{>b3ihms2FnE3j&4o2U3jaGzc+zA|J^7H<J7lPf^b>x$yikx=vpYppT#jc`ApQmrtyA1$rJa9NuEtnX3Yq1aIF3O%N'
    '+Er4YM~Yu3T)Zs0Gz{V3}X_0;PjX(mOWrkgA1P$+@EyQ#~J+GP+v1|7^f$VhEG;=YQWQrU#m|uhbZDkc3i$A_#Nd20^#g4v0I|38'
    '`o(!<<r_!4U8G_Q)7lyRVSWV*L(*x@{~VmwI{p#ci^bZkySo3e@lt?NR*DOgSP14ah;@K?e$fbc~O9d*YiW`>gB>=NW$V>?Y6kVq'
    'vNx8ex}RQ`Vz2Dm!Qn&#;I>hb;6@sM<s2Ogy4H8&RZoK|{E+Wa3Dw3S+52Kd7b1KHhZ6bK*K;M5NCK>UNd(PT8K+=MX6#XA0kwy%'
    '_f!AxUh3;nLwix$T0UG;#s-'
    'ccpMMsVY7<_$kLj`@fz*gk4qFEV*6ab3$mA3f($oYLk<%036WtG?CILv35o58B)owsHb3F8&T0ALJ=K|gZku@Y0+sC1EN9bmMOjh'
    'NyEtTBcc?UE6TY6x5l}nQdAfdB>oY7NK?eje;>%V029V49eSLbxK_dZvL=A+3)Wo}{TU$zu)G>n86$RXUI+5B0_R{j<Psgv&#tVQ'
    '2gc8wZ!-P|11To!>>HYBa##wLDEG-?Q+;cwfzZ+BrIX6#D40{?$$*&fONA4{>dD_b#A#-'
    'cS94^FnGcT%qZMICA(0?VbXzlIlp^9}d9o59!iww3nWp6@I-'
    '~_b=ka;@T<ye932HDgt|~wttLumkI{|gDT>2S#PD%nL^BEE;Lvapls-'
    '{}<0F7Sm9tJdvX)YJzhUA0Alpi<^t%hX6bAeSOXbX4%g@j(t19%xeoP{!vE6^+b7cO_966Gt%YL@pvF&AAak%Xp2+8?Zhv}8RdJR'
    'E~aQCk|6NT&f9V00<A(I{$_@?Hvn3@z7Q)zoS6J6si8HLlE2?|{OwoTIoi{UXOaE$G;4M>AW%nfy(R1`#a)Mv-VO2Hv;`eNlM}cC'
    '_;mO)#<+sg#WoKtTX$16DaD!_0K7GC@_95Y*~A8}~Q{=@P=&5W#;YG(FMiiOnYpCqUT;sTG+_4@U}jS7hXT4Ru)wT{Trw&d1W>_z'
    'zFgS#gnGgbj8}G08#{xIo%r`XkMpCLf(*ektmMP&uHsi7)e7R*H0a11v<$I1)8?HTk;I(L(<?LBgL0QzJ!J<!#5qy^2FdQUkCtXe'
    'p#N|AT?M&HA~P)PbB#k!9ZQ9o$SH0*_~i=&nTL70pUj8QrTc#Hayo9VC?l)kk^>`Lf`wU{6Sr0{5l{imh*OMi|a4X#r$L3<8Y`L='
    '2QJMtg=5cpJclQ>nL~YwO<eMiLd&hr{rP8<v}(w+@_auVOT`moPq23jO)1=$=e(226o`YR+Kd1z!2m6dQ~uM$>FLRC=@$a#Ua|g%'
    'ivUR@h}i!)Gt0kpz@P^Tj&Ia>Cy6S})6a$vil&JyoKp;ggmppI+K!%vs4Jt5y&Lmx=@@v$#6Q)lx~l*=Vs;w+2&=9)pa0n4N|MCQ'
    'BkSWbaGh$*)<p)3wyhvx-UUuuKaJN@V1_(>c7IQAx^4+{7!5Z9Plpc~H=2cP6^q_1Q5)Bt2fQG4;(W{bc;Mgv@4_*JfL-'
    'C70Yv?V}rhOla7qq@{^+a%g>?P_GL{4%iS;h#^X{{!=A|k*%x|V%M%RNAZmj5Q6h0CXebB_FZ*_gV$5#=4OjkQci36mp1$p=MOVo'
    '^4zgb<Ug{l-~v&5g7PS`X)ejD30%}yVpoN*XujaI>OL^`K#>uWB4`$6F2X$7LF<?3)*{|&T^%h-'
    'nA0reZ)76Hb4M}V=N$nWG2(3Oebw7$5>VniQm%YLyE7rUvG$r*krLgCgbRX|!DaCnxqe~OM?5Yz+~N)zlEF7g;+TdaJL*XV(x{hF'
    'jHHP@A$TwYH{}_?3mEp&*TD1Q{7vNVUXWl-xR~OwTabDRX-vdCUFCP8-kz&>&l_t-3iOa2kKJZ47vfQ8@(O8NSe>i;{UCTK1-Cs-'
    '_9_bJX<io=%{L$cwfoK(yRXA8J6~>O4#ESCsBm3Z36cyUp!tlEkdcB)wLKmQ9ya@}6hdxR+rF-_k<|St{g#A#;mboe#KhHbHp_q4'
    'L`^C_l5}JDRnh4gYCTM}+zR*iIG9-ebU&!e1&z9`J5??x1^XljBSZoD*j*5Y0h;chyzpMdM6Y(O40MS<S{jwUrgj5-'
    'y(*PZudO4oEl>YBfu|$Z2iE4KUb6g4v2kUVs#8j-'
    'i6L+{A?I^pk^0#E$6dtj72ls0s&Vn05ox9;&fGekt0aXkUJLvPaNJlja#Rofc;5JmxAgU#0mPL24ug5$Or|0lw~DZ{IvGMbj@n}`'
    '65*Z|tRpcIpL(1oW||LuV~jf_Sl5u{M2+F$U9SpOAYn<-S`D3=4}DNrlTULMX`yjLw7XuUj<lJ}T(dT-'
    '62}BN=?v+(=Cy}*jAi3}dxf)JR2Vi>^{u}OI+8#k88PbSp&(V%Qi<|V6JJEwuCK+CeONBn{U8vstLO>|Wevw~*!RVEubXL?ivB}O'
    '4Q{)6){wJp)HB`%lROy8u>>pQa6Px{v%AKOjipe3H^K+H&QDH9%CD=EYsCwX@kW_-'
    '01Fh7S;q*=E3OE5NueTN);>PAL@_E1qN|T$33d#CKU|@RtEI%*qQ@iGk*dWGfT?F=pi<AOdjC#Z{Lr>e!U+J<B*hrpBTra#Jh<&*'
    'RLmze)Mrw8A7-'
    'SPRnBRG4g;tv+LP|laPN_ZsN}tNeLY>3cqq^eKrgKa;X)ljoEr_>GnFtJaxa5z`4XzT_|Z+EfG{blfUBuP`dqdCv}d_c_>v{OWs5'
    '2zxf^W78175=#`G=p0IbGR->kR2UY$<I?`(SCmF9=4>U&X+ONieSDX{KxD`!D{__C{(Tl)6w=}&f|&)GzhCgwVcDsGpd=qp})-'
    '?c<Kgqo*U?4iyWFb-zm{*@4rcW>@~T;7PksfzcsY*dq78e69_6NE)INQN;|z%sF>&=c3J_S1E!_({n-'
    'v*m87MY*UQgwD?Zv@93ma6y89imjqLtri)`Q@aS~Ol(TL(1l7#1fG+<V}jw(hFM=~MU`$rS|(9>(3S2ko2Cck`d1L7h|ndC;p$^|'
    'B%3qaoS3HvmTJDm0NRx9{O#JhEFTDPo+9kZom!O@z-'
    'Qa!SDA@3)Q)y|%U7i)Qy)M2=B(sp(@?molu~t!8K`y<4+%&k%m1vLbYq;IKmlc6s`xIjnqsXoZ9$L*r}R$y?5DXmY??dmUaIrdRu'
    'owdB14>u$4j$<FwVr20o%pBTcr*%B=&R2ElYnniNv=9#|$8dh(5C%gjvq+M$__j_#v!|@ii*U#n(x3#pDMRS;P?sYBiPPq~0NJWP'
    'P!Y2-'
    't3MgCf^Rb~###Tqg}BZ^cy!KrrKq^0Lf0m5nVPYFbe0TkOze_%5DA=g~tyM+H9#^ZDXP{b%}`M&HroG(Ta&>R$KX{PKNv=|4E6Ju'
    'lBB^xIX73}V_eh5&^)J+`@-%-3nMJnNhF)ytfuOuD*5Wn-'
    '+nsF*@jlQJc+dety*QOC;!Mu;kc?j4!s>j&S(pwh~0QF|+6j*ndtU#etT`q`kfGHXvgl!BNyF&hC;XMo@}vKJ|)C?W$|YDaTlk}='
    'B?bPL&HLR_U=e+{8Gd0j(li_%B+CCd8>$fA&(1*5t}OA;YtB^WN2WRl#dwnefEU4?I{+8kZ1`#H2hTCtFErwwGaQjbFEaCz;__mQ'
    'gZ&`VE^0o8ja0PkW1luzm_55UxqEbH*z)U6Wgd@Qx9UvjpSqU{yfi^v6+tz4(wm00Xdj3}Zn1U0J`3XF}HNCcH60&ofvA_$?FIM&'
    'CX9eJxoeQ2NIwKJo`-'
    '+=nh)w61$xn;5Vd0aHYLP=5_C3&2NHT@}aR*P9|zWkA*cY&NYfEV~rsazZMM#cxL>g7GDhQnn`%E5S6{7rQOFbygt3fmGFnX&B^)'
    '$kJ`Xae1A9UiZWiR$jzOV!!QOde;^D%mt%EJi8KmLpQL*obUJ5P5Cse$!gF(1CML4toK390E=lC}5gS1crq&)H2qdp5l^QA;iu$w'
    'H8sSG6A(76iq3Kyq5@j6pEKsYD_7ap=z-W0X514VlV+epRB-'
    'FP3<*Npe_kyDerRBU1y#bh1Dw*G%9gBh9XK*i_q8ITl!J}V$cbaWMth^e=xBVpzT?g6Fy-FA49?t;sA;98<7pX&IUXNTsQ@(Sf(p'
    '4`v}LF7G6u2g{(H<$f$z_l!ze#_6Yroz#lD@<~YhxZ#cp@@f%X#BP)}|8Mz{D5(3oZO-s`g_WD+M%Kkz85I{6>6b@i9>-'
    'zM!*BSP5ZKrL$sD|BHBCT-3T$5YfW`tyIys%PonA(NJ(jB0%F$uzw@axbNP=AWL-'
    '?Xva>9I9~nZSL;*z#vd6e20tWN4LgqxBhO&{k$r6sYP2p=jzId-YVC$#+T=PZin5y(qMb(mP92qr}H4?9*)_cNmoW18wGZ<X<W~6'
    '>63{!h|F2<O(N>QWcPhEW3lYmcxrmkz7ws0}9`W6UR8~V#AJor|kk7CR81BYlV}vu~k&)fKfejOD@HPk{B(G*BOYY0+M!Cyr?b8c'
    ';tGS5iX(;8ao$-'
    'L0RYEK9}9Qwk1LHmano&Lz`pt*w*shE={&$ub&lKT=!u}cfjSWo15;3%D|Jsc0Odgs8p}{cwn|gSV#v4KP<!pL*eHzF;}IYJp3QS'
    'e5Pd'
    )
)).decode("utf-8"))
_MODEL_MEAN = (0.7754330004241974, 0.42216157898077045, 4.845066681290341, 0.2690391571999746, 0.649190802764908, 0.5422947717626129, 0.38474963508282034)
_MODEL_SCALE = (0.9129241235454895, 0.4939040192317694, 3.256390917597153, 0.44346035797258343, 0.4421957956795104, 0.27905760741549984, 0.4865360761410145)
_MODEL_INTERCEPT = 1.245742223898873
_MODEL_COEFFICIENTS = (-0.9451280735752147, -0.4680272533738784, -1.4615973335974357, 0.599421907433265, -0.1545610769891183, 0.44222011734216526, -0.025622914463126638)
_MODEL_THRESHOLD = 0.8065185529227787
_SELLABLE = (
    "STRAWBERRY", "MELON", "MILK", "WOOL", "EGG",
    "TOMATO", "CARROT", "WHEAT", "FERTILIZER",
)
_PRODUCT_BY_ANIMAL = {"COW": "MILK", "SHEEP": "WOOL", "GOOSE": "EGG"}
_GLUT_WEIGHT = {
    "STRAWBERRY": 2.0, "MELON": 3.6, "MILK": 2.0, "WOOL": 3.2,
    "EGG": 1.5, "TOMATO": 1.3, "CARROT": 1.0, "WHEAT": 1.0,
    "FERTILIZER": 1.0,
}
_STATE = {0: {}, 1: {}}
_WEED_STATE = {0: {}, 1: {}}
_WEED_REPLAY_STEPS = 8


def _get(value, key, default=None):
    if isinstance(value, dict):
        return value.get(key, default)
    getter = getattr(value, "get", None)
    if callable(getter):
        return getter(key, default)
    return getattr(value, key, default)


def _copy_action(action):
    action = copy.deepcopy(action or {})
    return {
        "farmer": list(action.get("farmer") or ["PASS"]),
        "hands": [list(order or ["PASS"]) for order in (action.get("hands") or [])],
        "market": [list(order) for order in (action.get("market") or [])],
    }


def _tile_token(tile):
    if tile is None or isinstance(tile, str):
        return tile
    return tuple(
        (key, _get(tile, key))
        for key in (
            "kind", "crop", "animal", "growth", "yield_units",
            "fertilizer_available", "cared_today",
        )
        if _get(tile, key) is not None
    )


def _public_farm_distance(left, right):
    distance = 0
    if tuple(_get(left, "farmer", []) or []) != tuple(_get(right, "farmer", []) or []):
        distance += 2
    left_hands = [tuple(position or ()) for position in (_get(left, "hands", []) or [])]
    right_hands = [tuple(position or ()) for position in (_get(right, "hands", []) or [])]
    distance += 3 * abs(len(left_hands) - len(right_hands))
    distance += sum(a != b for a, b in zip(left_hands, right_hands))
    left_unlocks = set(_get(left, "unlocked_quadrants", []) or [])
    right_unlocks = set(_get(right, "unlocked_quadrants", []) or [])
    distance += 4 * len(left_unlocks.symmetric_difference(right_unlocks))
    left_tiles = _get(left, "tiles", []) or []
    right_tiles = _get(right, "tiles", []) or []
    for y in range(max(len(left_tiles), len(right_tiles))):
        left_row = left_tiles[y] if y < len(left_tiles) else []
        right_row = right_tiles[y] if y < len(right_tiles) else []
        for x in range(max(len(left_row), len(right_row))):
            a = _tile_token(left_row[x]) if x < len(left_row) else "MISSING"
            b = _tile_token(right_row[x]) if x < len(right_row) else "MISSING"
            distance += a != b
    return distance


def _mirror_probability(distance, money_gap, board_streak, step):
    values = (
        math.log1p(max(0.0, distance)),
        float(distance == 0),
        math.log1p(max(0.0, money_gap)),
        float(money_gap <= 5.0),
        min(max(0, board_streak), 96) / 96.0,
        min(max(0, step), 718) / 718.0,
        float(step >= 480),
    )
    logit = _MODEL_INTERCEPT + sum(
        coefficient * ((value - mean) / scale)
        for value, mean, scale, coefficient in zip(
            values, _MODEL_MEAN, _MODEL_SCALE, _MODEL_COEFFICIENTS
        )
    )
    return 1.0 / (1.0 + math.exp(-min(35.0, max(-35.0, logit))))


def _align_hands(action, obs):
    action = _copy_action(action)
    seat = 1 if int(_get(obs, "player", 0) or 0) == 1 else 0
    farms = list(_get(obs, "farms", []) or [])
    farm = farms[seat] if seat < len(farms) else {}
    expected = len(_get(farm, "hands", []) or [])
    hands = list(action.get("hands") or [])
    if len(hands) < expected:
        hands.extend([["PASS"] for _ in range(expected - len(hands))])
    action["hands"] = [list(order or ["PASS"]) for order in hands[:expected]]
    return action


def _tile_at(farm, position):
    try:
        x, y = int(position[0]), int(position[1])
        return (_get(farm, "tiles", []) or [])[y][x]
    except (IndexError, TypeError, ValueError):
        return "LOCKED"


def _trace_actor_action(step, actor):
    trace = _ACTIONS[min(max(int(step), 0), len(_ACTIONS) - 1)] or {}
    if actor == "farmer":
        return list(trace.get("farmer") or ["PASS"])
    hands = trace.get("hands", []) or []
    return list(hands[actor] if actor < len(hands) else ["PASS"])


def _weed_repair_action(obs, action, step):
    """Repair a visible fixed-route PLANT/BUILD slip without touching market."""
    action = _align_hands(action, obs)
    seat = 1 if int(_get(obs, "player", 0) or 0) == 1 else 0
    game = _WEED_STATE[seat]
    if step == 0 or step < game.get("last_step", -1):
        game = {"last_step": step, "active": {}}
        _WEED_STATE[seat] = game
    game["last_step"] = step

    farms = list(_get(obs, "farms", []) or [])
    farm = farms[seat] if seat < len(farms) else {}
    positions = [_get(farm, "farmer"), *list(_get(farm, "hands", []) or [])]
    unit_actions = [action.get("farmer", ["PASS"]), *list(action.get("hands") or [])]
    active = game["active"]

    for actor, transaction in list(active.items()):
        index = 0 if actor == "farmer" else int(actor) + 1
        if index >= len(unit_actions):
            active.pop(actor, None)
            continue
        age = step - transaction["start"]
        if age == 1:
            unit_actions[index] = list(transaction["intended"])
        elif 2 <= age <= 1 + _WEED_REPLAY_STEPS:
            unit_actions[index] = _trace_actor_action(step - 1, actor)
        else:
            active.pop(actor, None)

    for index, (position, intended) in enumerate(zip(positions, unit_actions)):
        actor = "farmer" if index == 0 else index - 1
        if actor in active or not isinstance(intended, list) or not intended:
            continue
        operation = intended[0]
        if operation not in ("BUILD_PASTURE", "PLANT"):
            continue
        tile = _tile_at(farm, position)
        if not isinstance(tile, dict) or tile.get("kind") != "WEED":
            continue
        active[actor] = {"start": step, "intended": list(intended)}
        unit_actions[index] = ["DIG"]

    action["farmer"] = unit_actions[0] if unit_actions else ["PASS"]
    action["hands"] = unit_actions[1:]
    return _align_hands(action, obs)


def _shed_access(size):
    half = size // 2
    return {
        (half - 1, half - 1), (half, half - 1),
        (half - 1, half), (half, half),
    }


def _projected_shed(obs, action):
    seat = 1 if int(_get(obs, "player", 0) or 0) == 1 else 0
    farms = list(_get(obs, "farms", []) or [])
    farm = farms[seat] if seat < len(farms) else {}
    private = _get(obs, "private", {}) or {}
    projected = {
        key: max(0, int(value or 0))
        for key, value in dict(_get(private, "shed", {}) or {}).items()
    }
    inventories = list(_get(private, "inventories", []) or [])
    positions = [_get(farm, "farmer", [0, 0]), *list(_get(farm, "hands", []) or [])]
    actions = [action.get("farmer", ["PASS"]), *list(action.get("hands") or [])]
    tiles = list(_get(farm, "tiles", []) or [])
    access = _shed_access(len(tiles) or 10)
    for index, unit_action in enumerate(actions):
        if index >= len(positions) or index >= len(inventories):
            continue
        position = positions[index]
        if not isinstance(position, (list, tuple)) or len(position) < 2:
            continue
        x, y = int(position[0]), int(position[1])
        if (x, y) not in access or not (0 <= y < len(tiles) and 0 <= x < len(tiles[y])):
            continue
        if tiles[y][x] == "LOCKED" or not isinstance(unit_action, list) or not unit_action:
            continue
        inventory = {
            key: max(0, int(value or 0))
            for key, value in dict(inventories[index] or {}).items()
        }
        if unit_action[0] == "DROP":
            deposits = inventory.items()
        elif unit_action[0] == "PLACE" and len(unit_action) >= 2:
            item = unit_action[1]
            tile = tiles[y][x]
            structure = {"COW": "PASTURE", "SHEEP": "PASTURE", "GOOSE": "COOP"}.get(item)
            if (
                structure is not None and isinstance(tile, dict)
                and tile.get("kind") == structure and "animal" not in tile
            ):
                continue
            try:
                requested = int(unit_action[2]) if len(unit_action) >= 3 else 1
            except (TypeError, ValueError):
                continue
            deposits = ((item, min(max(0, requested), inventory.get(item, 0))),)
        else:
            continue
        for item, quantity in deposits:
            room = max(0, 100 - sum(projected.values()))
            amount = min(max(0, int(quantity or 0)), room)
            if amount:
                projected[item] = projected.get(item, 0) + amount
    return projected


def _opponent_exposure(obs):
    seat = 1 if int(_get(obs, "player", 0) or 0) == 1 else 0
    farms = list(_get(obs, "farms", []) or [])
    opponent = farms[1 - seat] if len(farms) >= 2 else {}
    exposure = {item: 0.0 for item in _SELLABLE}
    for row in (_get(opponent, "tiles", []) or []):
        for tile in row if isinstance(row, list) else [row]:
            if not isinstance(tile, dict):
                continue
            crop = str(tile.get("crop", "")).upper()
            if crop in exposure:
                exposure[crop] += max(1.0, float(tile.get("yield_units", 0) or 0))
            product = _PRODUCT_BY_ANIMAL.get(str(tile.get("animal", "")).upper())
            if product:
                exposure[product] += 1.0 + max(0.0, float(tile.get("yield_units", 0) or 0))
            if tile.get("fertilizer_available", False):
                exposure["FERTILIZER"] += 1.0
    return exposure


def _ranked_sells(obs, action, requested=None):
    shed = _projected_shed(obs, action)
    if requested is None:
        requested = {item: int(shed.get(item, 0) or 0) for item in _SELLABLE}
    prices = _get(_get(obs, "market", {}) or {}, "prices", {}) or {}
    exposure = _opponent_exposure(obs)
    rows = []
    for index, item in enumerate(_SELLABLE):
        quantity = min(max(0, int(requested.get(item, 0) or 0)), max(0, int(shed.get(item, 0) or 0)))
        if quantity <= 0:
            continue
        score = (
            (1.0 + exposure.get(item, 0.0))
            * _GLUT_WEIGHT.get(item, 1.0)
            * max(1.0, float(prices.get(item, 1) or 1))
            * math.log1p(quantity)
        )
        rows.append((score, -index, item, quantity))
    rows.sort(reverse=True)
    return [["SELL", item, quantity] for _, _, item, quantity in rows]


def _front_run_market(obs, action):
    action = _align_hands(action, obs)
    market = [list(order) for order in (action.get("market") or [])]
    requests = {}
    for order in market:
        if len(order) >= 3 and order[0] == "SELL":
            try:
                quantity = max(0, int(order[2]))
            except (TypeError, ValueError):
                quantity = 0
            requests[order[1]] = requests.get(order[1], 0) + quantity
    sells = _ranked_sells(obs, action, requests)
    targeted = {order[1] for order in sells}
    remainder = [
        order for order in market
        if not (len(order) >= 3 and order[0] == "SELL" and order[1] in targeted)
    ]
    action["market"] = (sells + remainder)[:10]
    return action


def _terminal_market(obs, action):
    action = _align_hands(action, obs)
    action["market"] = _ranked_sells(obs, action)[:10]
    return action


def _hybrid_action(obs, action, step):
    seat = 1 if int(_get(obs, "player", 0) or 0) == 1 else 0
    game = _STATE[seat]
    if step == 0 or step < game.get("last_step", -1):
        game = {
            "board_streak": 0, "latched": False, "divergence": 0,
            "last_step": step, "mode": None,
        }
        _STATE[seat] = game
    game["last_step"] = step
    farms = list(_get(obs, "farms", []) or [])
    distance = _public_farm_distance(farms[0], farms[1]) if len(farms) >= 2 else math.inf
    money_gap = (
        abs(float(_get(farms[0], "money", 0) or 0) - float(_get(farms[1], "money", 0) or 0))
        if len(farms) >= 2 else math.inf
    )
    game["board_streak"] = game["board_streak"] + 1 if distance <= 2 else 0
    probability = _mirror_probability(distance, money_gap, game["board_streak"], step)
    selected = probability >= _MODEL_THRESHOLD
    if selected and not game["latched"]:
        game["latched"] = True
    if game["latched"]:
        game["divergence"] = 0 if distance <= 2 else game["divergence"] + 1
        if game["divergence"] >= 8:
            game["latched"] = False
            game["divergence"] = 0
    if game["mode"] is None and step >= 48:
        game["mode"] = "mirror" if selected else "open"
    active = step >= 24 and (
        game["latched"] if game["mode"] == "mirror" else game["mode"] == "open"
    )
    if active:
        action = _front_run_market(obs, action)
    if step == 718:
        action = _terminal_market(obs, action)
    return _align_hands(action, obs)


def agent(obs):
    """Kaggle one-argument entry point."""
    try:
        step = min(max(0, int(_get(obs, "step", 0) or 0)), len(_ACTIONS) - 1)
        action = _weed_repair_action(obs, _copy_action(_ACTIONS[step]), step)
        return _hybrid_action(obs, action, step)
    except Exception:
        seat = 1 if int(_get(obs, "player", 0) or 0) == 1 else 0
        farms = list(_get(obs, "farms", []) or [])
        farm = farms[seat] if seat < len(farms) else {}
        return {
            "farmer": ["PASS"],
            "hands": [["PASS"] for _ in (_get(farm, "hands", []) or [])],
            "market": [],
        }


def _kaggle_submission_entrypoint(obs):
    return agent(obs)
