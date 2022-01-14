#pragma once

#include <vector>
#include <unordered_map>
#include <string_view>

#include "platform/Types.h"
#include "sameboy/semver.hpp"

namespace rp::lsdj {

struct RomVersionDesc {
	semver::version version;
	uint32 offsetGroup;
	std::string_view tags;
};

struct OffsetDesc {
	uint32 active;
	uint32 phrase;
	uint32 chain;
	uint32 song;
	uint32 cursorX;
	uint32 cursorY;
	uint32 screenX;
	uint32 screenY;
};

static const std::vector<OffsetDesc> OFFSET_GROUPS = {
	{ 724, 915, 923, 927, 492, 493 },
	{ 723, 914, 922, 926, 492, 493 },
	{ 722, 913, 921, 925, 491, 492 },
	{ 789, 980, 988, 992, 558, 559 },
	{ 722, 913, 921, 925, 491, 492 },
	{ 468, 659, 667, 671, 237, 238 },
	{ 466, 657, 665, 669, 235, 236 },
	{ 461, 652, 660, 664, 232, 233 },
	{ 528, 719, 727, 731, 299, 300 },
	{ 461, 652, 660, 664, 232, 233 },
	{ 462, 653, 661, 665, 233, 234 },
	{ 531, 722, 730, 734, 302, 303 },
	{ 462, 653, 661, 665, 233, 234 },
	{ 232, 364, 380, 512, 859, 860 },
	{ 232, 364, 380, 512, 867, 868 },
	{ 232, 364, 380, 512, 879, 880 },
	{ 232, 364, 380, 512, 890, 891 },
	{ 232, 364, 380, 512, 882, 883 },
	{ 232, 364, 380, 512, 893, 894 },
	{ 232, 364, 380, 512, 962, 963 },
	{ 232, 364, 380, 512, 893, 894 },
	{ 232, 364, 380, 512, 899, 900 },
	{ 232, 364, 380, 512, 901, 902 },
	{ 232, 364, 380, 512, 907, 908 },
	{ 232, 364, 380, 512, 976, 977 },
	{ 232, 364, 380, 512, 907, 908 },
	{ 232, 364, 380, 512, 976, 977 },
	{ 232, 364, 380, 512, 907, 908 },
	{ 232, 364, 380, 512, 906, 907 },
	{ 232, 364, 380, 512, 969, 970 },
	{ 232, 364, 380, 512, 900, 901 },
	{ 232, 364, 380, 512, 899, 900 },
	{ 232, 364, 380, 512, 884, 885 },
	{ 232, 364, 380, 512, 887, 888 },
	{ 232, 364, 380, 512, 955, 956 },
	{ 232, 364, 380, 512, 887, 888 },
	{ 232, 364, 380, 512, 955, 956 },
	{ 232, 364, 380, 512, 887, 888 },
	{ 232, 364, 380, 512, 955, 956 },
	{ 232, 364, 380, 512, 887, 888 },
	{ 232, 364, 380, 512, 894, 895 },
	{ 232, 364, 380, 512, 892, 893 },
	{ 232, 364, 380, 512, 891, 892 },
	{ 232, 364, 380, 512, 900, 901 },
	{ 224, 364, 380, 512, 900, 901 },
	{ 224, 364, 380, 512, 901, 902 },
	{ 224, 364, 380, 512, 900, 901 },
	{ 224, 364, 380, 512, 897, 898 },
	{ 224, 364, 380, 512, 898, 899 },
	{ 224, 364, 380, 512, 895, 896 },
	{ 224, 364, 380, 512, 919, 920 },
	{ 224, 364, 380, 512, 1043, 1044 },
	{ 224, 364, 380, 512, 1046, 1047 },
	{ 224, 364, 380, 512, 1050, 1051 },
	{ 224, 364, 380, 512, 1051, 1052 },
	{ 224, 364, 380, 512, 1050, 1051 },
	{ 224, 364, 380, 512, 1051, 1052 },
	{ 224, 364, 380, 512, 1052, 1053 },
	{ 224, 364, 380, 512, 1051, 1052 },
	{ 224, 364, 380, 512, 1056, 1057 },
	{ 224, 364, 380, 512, 1124, 1125 },
	{ 224, 364, 380, 512, 1056, 1057 },
	{ 224, 364, 380, 512, 1054, 1055 },
};

static const std::unordered_map<uint32, RomVersionDesc> VERSION_LOOKUP = {
	{ 39296686, { semver::version { 4, 0, 0 }, 0 }},
	{ 2059020996, { semver::version { 4, 0, 1 }, 0 }},
	{ 2638915703, { semver::version { 4, 0, 2 }, 0 }},
	{ 538935, { semver::version { 4, 0, 3 }, 0 }},
	{ 1984007071, { semver::version { 4, 0, 4 }, 0, "stable" }},
	{ 2022126599, { semver::version { 4, 0, 5 }, 1 }},
	{ 2944119784, { semver::version { 4, 0, 6 }, 1 }},
	{ 2579031444, { semver::version { 4, 0, 7 }, 1 }},
	{ 4189990103, { semver::version { 4, 0, 8 }, 2 }},
	{ 688256112, { semver::version { 4, 1, 0 }, 3, "arduinoboy" }},
	{ 3959562744, { semver::version { 4, 1, 0 }, 4, "stable" }},
	{ 1950382289, { semver::version { 4, 2, 1 }, 5 }},
	{ 1958441858, { semver::version { 4, 2, 4 }, 5 }},
	{ 1232046375, { semver::version { 4, 2, 5 }, 5 }},
	{ 2216944496, { semver::version { 4, 2, 6 }, 5 }},
	{ 3626773062, { semver::version { 4, 2, 7 }, 5 }},
	{ 887685837, { semver::version { 4, 2, 8 }, 5 }},
	{ 2582777469, { semver::version { 4, 2, 9 }, 5 }},
	{ 84555853, { semver::version { 4, 3, 0 }, 5, "stable" }},
	{ 3196402771, { semver::version { 4, 3, 1 }, 5 }},
	{ 108717199, { semver::version { 4, 3, 2 }, 5 }},
	{ 4262420365, { semver::version { 4, 3, 3 }, 6 }},
	{ 2998307246, { semver::version { 4, 3, 4 }, 6 }},
	{ 1046392657, { semver::version { 4, 3, 5 }, 6 }},
	{ 664811206, { semver::version { 4, 3, 6 }, 6 }},
	{ 3272789640, { semver::version { 4, 3, 7 }, 6 }},
	{ 736689752, { semver::version { 4, 3, 8 }, 6 }},
	{ 3390399744, { semver::version { 4, 3, 8 }, 6 }},
	{ 2716551436, { semver::version { 4, 4, 0 }, 6, "stable" }},
	{ 3053776015, { semver::version { 4, 4, 3 }, 6 }},
	{ 2645371428, { semver::version { 4, 4, 9 }, 7 }},
	{ 2385103784, { semver::version { 4, 5, 0 }, 7 }},
	{ 2723966608, { semver::version { 4, 5, 1 }, 7 }},
	{ 2429353599, { semver::version { 4, 5, 3 }, 7 }},
	{ 388846825, { semver::version { 4, 5, 4 }, 7, "stable" }},
	{ 1519526617, { semver::version { 4, 5, 5 }, 7 }},
	{ 943919719, { semver::version { 4, 5, 6 }, 7 }},
	{ 460419239, { semver::version { 4, 5, 7 }, 7 }},
	{ 83692492, { semver::version { 4, 5, 8 }, 7 }},
	{ 3972784090, { semver::version { 4, 5, 9 }, 7 }},
	{ 1948799539, { semver::version { 4, 6, 0 }, 7, "stable" }},
	{ 2019060452, { semver::version { 4, 6, 1 }, 7 }},
	{ 1630068441, { semver::version { 4, 6, 2 }, 7, "stable" }},
	{ 3406020554, { semver::version { 4, 6, 3 }, 7 }},
	{ 610824616, { semver::version { 4, 6, 4 }, 7 }},
	{ 1818706830, { semver::version { 4, 6, 5 }, 7 }},
	{ 2333872139, { semver::version { 4, 6, 6 }, 7 }},
	{ 2401390913, { semver::version { 4, 6, 7 }, 7 }},
	{ 3170340609, { semver::version { 4, 6, 8 }, 7 }},
	{ 1572498013, { semver::version { 4, 6, 9 }, 7, "stable" }},
	{ 995843178, { semver::version { 4, 7, 0 }, 7 }},
	{ 697798189, { semver::version { 4, 7, 2 }, 7 }},
	{ 209555959, { semver::version { 4, 7, 3 }, 8, "arduinoboy" }},
	{ 181131276, { semver::version { 4, 7, 3 }, 9, "stable" }},
	{ 1803864400, { semver::version { 4, 7, 4 }, 9 }},
	{ 725179447, { semver::version { 4, 7, 5 }, 9 }},
	{ 3669004823, { semver::version { 4, 8, 0 }, 10, "stable" }},
	{ 550345484, { semver::version { 4, 8, 1 }, 10 }},
	{ 903228375, { semver::version { 4, 8, 2 }, 10 }},
	{ 4258394291, { semver::version { 4, 8, 3 }, 10 }},
	{ 1927208215, { semver::version { 4, 8, 4 }, 10 }},
	{ 158912650, { semver::version { 4, 8, 5 }, 10 }},
	{ 706364741, { semver::version { 4, 8, 6 }, 10 }},
	{ 2105126070, { semver::version { 4, 8, 7 }, 10 }},
	{ 3797405943, { semver::version { 4, 8, 8 }, 11, "arduinoboy" }},
	{ 1564072429, { semver::version { 4, 8, 8 }, 12 }},
	{ 3143463926, { semver::version { 4, 8, 9 }, 12 }},
	{ 124251349, { semver::version { 4, 9, 0 }, 12 }},
	{ 2820205132, { semver::version { 4, 9, 1 }, 12 }},
	{ 3533602874, { semver::version { 4, 9, 2 }, 12 }},
	{ 3117007861, { semver::version { 4, 9, 3 }, 12 }},
	{ 2494111220, { semver::version { 4, 9, 4 }, 12, "goomba" }},
	{ 918683067, { semver::version { 4, 9, 4 }, 12, "stable" }},
	{ 1977007611, { semver::version { 4, 9, 5 }, 12 }},
	{ 2288041085, { semver::version { 4, 9, 6 }, 13 }},
	{ 1314215630, { semver::version { 4, 9, 7 }, 14 }},
	{ 749591699, { semver::version { 4, 9, 8 }, 14 }},
	{ 610539556, { semver::version { 4, 9, 9 }, 14 }},
	{ 1205724580, { semver::version { 5, 0, 0 }, 15 }},
	{ 4025522530, { semver::version { 5, 0, 1 }, 15 }},
	{ 3012862621, { semver::version { 5, 0, 2 }, 15 }},
	{ 1007349763, { semver::version { 5, 0, 3 }, 15, "stable" }},
	{ 3386968737, { semver::version { 5, 1, 0 }, 16 }},
	{ 3017151099, { semver::version { 5, 1, 1 }, 17 }},
	{ 3441786130, { semver::version { 5, 1, 2 }, 17 }},
	{ 241481822, { semver::version { 5, 1, 3 }, 17 }},
	{ 595777833, { semver::version { 5, 1, 4 }, 17 }},
	{ 3809410389, { semver::version { 5, 1, 5 }, 17 }},
	{ 478347039, { semver::version { 5, 1, 6 }, 17 }},
	{ 2633165547, { semver::version { 5, 1, 7 }, 17 }},
	{ 405349081, { semver::version { 5, 1, 8 }, 17 }},
	{ 520174013, { semver::version { 5, 1, 8 }, 17 }},
	{ 3333288295, { semver::version { 5, 2, 0 }, 17 }},
	{ 3535717925, { semver::version { 5, 2, 1 }, 17 }},
	{ 2572960100, { semver::version { 5, 2, 2 }, 17 }},
	{ 3821521277, { semver::version { 5, 2, 3 }, 17 }},
	{ 2954706488, { semver::version { 5, 2, 4 }, 17 }},
	{ 3161650456, { semver::version { 5, 2, 5 }, 18 }},
	{ 1478247915, { semver::version { 5, 2, 6 }, 18 }},
	{ 1809451499, { semver::version { 5, 2, 7 }, 18 }},
	{ 3495790725, { semver::version { 5, 2, 8 }, 18 }},
	{ 3236311661, { semver::version { 5, 2, 9 }, 18 }},
	{ 3860491040, { semver::version { 5, 3, 0 }, 18 }},
	{ 1054041234, { semver::version { 5, 3, 1 }, 18 }},
	{ 3319690365, { semver::version { 5, 3, 2 }, 18 }},
	{ 334255846, { semver::version { 5, 3, 3 }, 18 }},
	{ 3542357809, { semver::version { 5, 3, 4 }, 19, "arduinoboy" }},
	{ 1282750438, { semver::version { 5, 3, 4 }, 20 }},
	{ 2921731652, { semver::version { 5, 3, 5 }, 20 }},
	{ 1694601527, { semver::version { 5, 3, 6 }, 20 }},
	{ 1456481104, { semver::version { 5, 3, 7 }, 20 }},
	{ 436560336, { semver::version { 5, 3, 8 }, 20 }},
	{ 971872308, { semver::version { 5, 3, 9 }, 21 }},
	{ 3653401647, { semver::version { 5, 4, 0 }, 21 }},
	{ 2845522341, { semver::version { 5, 4, 1 }, 22 }},
	{ 1865322863, { semver::version { 5, 4, 2 }, 22 }},
	{ 2403169696, { semver::version { 5, 4, 3 }, 22 }},
	{ 905528789, { semver::version { 5, 4, 4 }, 22 }},
	{ 1536328810, { semver::version { 5, 4, 5 }, 22 }},
	{ 1627456997, { semver::version { 5, 4, 6 }, 22 }},
	{ 762833157, { semver::version { 5, 4, 7 }, 22 }},
	{ 3751162619, { semver::version { 5, 4, 8 }, 22 }},
	{ 390815300, { semver::version { 5, 4, 9 }, 22 }},
	{ 2439010233, { semver::version { 5, 5, 0 }, 22 }},
	{ 4172814710, { semver::version { 5, 5, 1 }, 22 }},
	{ 852066409, { semver::version { 5, 5, 2 }, 22 }},
	{ 4124073074, { semver::version { 5, 5, 3 }, 22 }},
	{ 2483396277, { semver::version { 5, 5, 4 }, 22 }},
	{ 285951192, { semver::version { 5, 5, 5 }, 22 }},
	{ 3804627680, { semver::version { 5, 5, 6 }, 22 }},
	{ 905193113, { semver::version { 5, 5, 7 }, 22 }},
	{ 1808638901, { semver::version { 5, 5, 8 }, 22 }},
	{ 1855666317, { semver::version { 5, 5, 9 }, 22 }},
	{ 2222423415, { semver::version { 5, 6, 0 }, 22 }},
	{ 2311886, { semver::version { 5, 6, 1 }, 22 }},
	{ 2373023443, { semver::version { 5, 6, 2 }, 22 }},
	{ 3620892307, { semver::version { 5, 6, 3 }, 22 }},
	{ 3606578429, { semver::version { 5, 6, 4 }, 22 }},
	{ 1290473849, { semver::version { 5, 6, 5 }, 22 }},
	{ 4065310794, { semver::version { 5, 7, 0 }, 23 }},
	{ 2787200037, { semver::version { 5, 7, 1 }, 23 }},
	{ 1604690905, { semver::version { 5, 7, 2 }, 23 }},
	{ 2544381527, { semver::version { 5, 7, 3 }, 23 }},
	{ 1371181006, { semver::version { 5, 7, 4 }, 23 }},
	{ 1136073671, { semver::version { 5, 7, 5 }, 23 }},
	{ 2805555324, { semver::version { 5, 7, 6 }, 23 }},
	{ 1316801502, { semver::version { 5, 7, 7 }, 23 }},
	{ 1235268420, { semver::version { 5, 7, 8 }, 24, "arduinoboy-untested" }},
	{ 6243999, { semver::version { 5, 7, 8 }, 25, "stable" }},
	{ 3655525793, { semver::version { 5, 8, 1 }, 25 }},
	{ 1003020052, { semver::version { 5, 8, 2 }, 25 }},
	{ 2763727461, { semver::version { 5, 8, 3 }, 25 }},
	{ 1243354196, { semver::version { 5, 8, 4 }, 25 }},
	{ 3747779846, { semver::version { 5, 8, 5 }, 25 }},
	{ 3680773124, { semver::version { 5, 8, 6 }, 25 }},
	{ 1885695272, { semver::version { 5, 8, 7 }, 25 }},
	{ 525322023, { semver::version { 5, 8, 8 }, 26, "arduinoboy-untested" }},
	{ 2314834860, { semver::version { 5, 8, 8 }, 27, "stable" }},
	{ 3822008679, { semver::version { 5, 8, 9 }, 27 }},
	{ 325287058, { semver::version { 5, 9, 0 }, 27 }},
	{ 1510371123, { semver::version { 5, 9, 1 }, 27 }},
	{ 3899827417, { semver::version { 5, 9, 2 }, 27 }},
	{ 2177379632, { semver::version { 5, 9, 3 }, 27 }},
	{ 3983570745, { semver::version { 5, 9, 4 }, 27 }},
	{ 1533368742, { semver::version { 5, 9, 5 }, 27 }},
	{ 870139031, { semver::version { 5, 9, 6 }, 27 }},
	{ 3551474807, { semver::version { 5, 9, 7 }, 27 }},
	{ 3482383440, { semver::version { 5, 9, 8 }, 28 }},
	{ 1613663085, { semver::version { 5, 9, 9 }, 29, "arduinoboy" }},
	{ 155227470, { semver::version { 5, 9, 9 }, 30, "stable" }},
	{ 2182303442, { semver::version { 6, 0, 0 }, 30 }},
	{ 3659045881, { semver::version { 6, 0, 1 }, 30, "stable" }},
	{ 1604594231, { semver::version { 6, 0, 2 }, 30 }},
	{ 3813667656, { semver::version { 6, 0, 3 }, 30 }},
	{ 3059164471, { semver::version { 6, 0, 4 }, 30 }},
	{ 3845675453, { semver::version { 6, 0, 5 }, 30 }},
	{ 3041122975, { semver::version { 6, 0, 6 }, 30 }},
	{ 1916545498, { semver::version { 6, 0, 7 }, 30 }},
	{ 3952745460, { semver::version { 6, 0, 8 }, 30, "send-notes-in-off" }},
	{ 973723366, { semver::version { 6, 0, 8 }, 30 }},
	{ 2159835984, { semver::version { 6, 0, 9 }, 30 }},
	{ 2539137332, { semver::version { 6, 1, 0 }, 30 }},
	{ 3110087533, { semver::version { 6, 1, 1 }, 30 }},
	{ 1152477476, { semver::version { 6, 1, 2 }, 30 }},
	{ 1172033225, { semver::version { 6, 1, 3 }, 30 }},
	{ 506594297, { semver::version { 6, 1, 4 }, 31 }},
	{ 2679919351, { semver::version { 6, 1, 5 }, 31 }},
	{ 2195077021, { semver::version { 6, 1, 6 }, 31 }},
	{ 1941329111, { semver::version { 6, 1, 7 }, 31 }},
	{ 4023035233, { semver::version { 6, 1, 8 }, 31 }},
	{ 3255371185, { semver::version { 6, 1, 9 }, 31 }},
	{ 2016000901, { semver::version { 6, 2, 0 }, 31 }},
	{ 963034004, { semver::version { 6, 3, 0 }, 31 }},
	{ 1996478921, { semver::version { 6, 3, 1 }, 31 }},
	{ 348833419, { semver::version { 6, 3, 2 }, 31 }},
	{ 4449154, { semver::version { 6, 3, 3 }, 32 }},
	{ 2768972616, { semver::version { 6, 3, 4 }, 32 }},
	{ 475913188, { semver::version { 6, 3, 5 }, 32 }},
	{ 3337410515, { semver::version { 6, 3, 6 }, 32 }},
	{ 1830902959, { semver::version { 6, 3, 7 }, 32 }},
	{ 2952738886, { semver::version { 6, 3, 8 }, 32 }},
	{ 474275620, { semver::version { 6, 3, 9 }, 32 }},
	{ 5860951, { semver::version { 6, 4, 0 }, 32 }},
	{ 2084061444, { semver::version { 6, 4, 1 }, 32 }},
	{ 642140223, { semver::version { 6, 4, 2 }, 32 }},
	{ 1967801952, { semver::version { 6, 4, 3 }, 32, "serial-send-i" }},
	{ 1671641188, { semver::version { 6, 4, 3 }, 32 }},
	{ 3237063936, { semver::version { 6, 4, 4 }, 32 }},
	{ 3392516443, { semver::version { 6, 4, 5 }, 32, "stable" }},
	{ 3261672541, { semver::version { 6, 4, 5 }, 32 }},
	{ 2600314736, { semver::version { 6, 4, 9 }, 33 }},
	{ 2108463009, { semver::version { 6, 5, 0 }, 33 }},
	{ 4114352118, { semver::version { 6, 5, 1 }, 33 }},
	{ 4234250371, { semver::version { 6, 6, 0 }, 33 }},
	{ 782155833, { semver::version { 6, 6, 1 }, 33 }},
	{ 1263570315, { semver::version { 6, 6, 2 }, 33 }},
	{ 2114010035, { semver::version { 6, 6, 3 }, 33 }},
	{ 2990579072, { semver::version { 6, 6, 4 }, 33 }},
	{ 3154607343, { semver::version { 6, 6, 5 }, 33 }},
	{ 3662778811, { semver::version { 6, 6, 6 }, 33 }},
	{ 1137785681, { semver::version { 6, 6, 7 }, 33 }},
	{ 2105797404, { semver::version { 6, 6, 8 }, 33 }},
	{ 2239494406, { semver::version { 6, 8, 0 }, 33 }},
	{ 632980823, { semver::version { 6, 8, 1 }, 33 }},
	{ 632966096, { semver::version { 6, 8, 2 }, 33, "stable" }},
	{ 1480450992, { semver::version { 6, 8, 3 }, 33 }},
	{ 1144846915, { semver::version { 6, 8, 4 }, 33 }},
	{ 3518652119, { semver::version { 6, 8, 5 }, 34, "arduinoboy" }},
	{ 1028379768, { semver::version { 6, 8, 5 }, 35 }},
	{ 166619133, { semver::version { 6, 8, 6 }, 36, "arduinoboy" }},
	{ 1872142788, { semver::version { 6, 8, 6 }, 37 }},
	{ 2608385697, { semver::version { 6, 8, 7 }, 37 }},
	{ 2901872813, { semver::version { 6, 8, 8 }, 37 }},
	{ 2407936052, { semver::version { 6, 9, 0 }, 38, "arduinoboy" }},
	{ 1290926105, { semver::version { 6, 9, 0 }, 39 }},
	{ 2268008317, { semver::version { 7, 0, 0 }, 39 }},
	{ 823063395, { semver::version { 7, 0, 1 }, 39 }},
	{ 3622784947, { semver::version { 7, 0, 2 }, 39 }},
	{ 1713271519, { semver::version { 7, 0, 2 }, 39 }},
	{ 643709424, { semver::version { 7, 0, 4 }, 39 }},
	{ 2654681112, { semver::version { 7, 0, 5 }, 39 }},
	{ 298727574, { semver::version { 7, 0, 6 }, 39 }},
	{ 4086493903, { semver::version { 7, 0, 7 }, 39 }},
	{ 2976171729, { semver::version { 7, 0, 8 }, 39 }},
	{ 3473270526, { semver::version { 7, 1, 0 }, 39 }},
	{ 2985942800, { semver::version { 7, 1, 1 }, 39 }},
	{ 2214361665, { semver::version { 7, 1, 2 }, 39 }},
	{ 2847201065, { semver::version { 7, 1, 3 }, 39 }},
	{ 2154813566, { semver::version { 7, 1, 4 }, 39 }},
	{ 3751037464, { semver::version { 7, 1, 5 }, 39 }},
	{ 2741495508, { semver::version { 7, 1, 6 }, 39 }},
	{ 3869155582, { semver::version { 7, 1, 7 }, 39 }},
	{ 476565157, { semver::version { 7, 1, 8 }, 39 }},
	{ 3531124890, { semver::version { 7, 1, 9 }, 39 }},
	{ 3637670064, { semver::version { 7, 2, 0 }, 39 }},
	{ 878071470, { semver::version { 7, 2, 1 }, 39 }},
	{ 992888127, { semver::version { 7, 2, 2 }, 39 }},
	{ 2098171474, { semver::version { 7, 2, 3 }, 39 }},
	{ 2571187584, { semver::version { 7, 2, 4 }, 39 }},
	{ 1432439373, { semver::version { 7, 2, 5 }, 39 }},
	{ 3251297332, { semver::version { 7, 2, 6 }, 39 }},
	{ 687227468, { semver::version { 7, 2, 7 }, 39 }},
	{ 2332452005, { semver::version { 7, 2, 8 }, 39 }},
	{ 3375470170, { semver::version { 7, 2, 9 }, 39 }},
	{ 1587173377, { semver::version { 7, 3, 0 }, 39 }},
	{ 2345161968, { semver::version { 7, 3, 1 }, 39 }},
	{ 3796544274, { semver::version { 7, 3, 2 }, 39 }},
	{ 1793642926, { semver::version { 7, 3, 3 }, 39 }},
	{ 1057233033, { semver::version { 7, 4, 0 }, 40 }},
	{ 3514920935, { semver::version { 7, 4, 1 }, 40 }},
	{ 3016745268, { semver::version { 7, 4, 2 }, 41 }},
	{ 667286258, { semver::version { 7, 4, 3 }, 42 }},
	{ 2917138068, { semver::version { 7, 4, 4 }, 42 }},
	{ 1876740143, { semver::version { 7, 4, 4 }, 42 }},
	{ 2736328043, { semver::version { 7, 5, 1 }, 42 }},
	{ 1624634227, { semver::version { 7, 5, 2 }, 42 }},
	{ 4069724275, { semver::version { 7, 5, 3 }, 42 }},
	{ 1897970720, { semver::version { 7, 5, 4 }, 42 }},
	{ 3614827108, { semver::version { 7, 5, 5 }, 43 }},
	{ 3933582835, { semver::version { 7, 5, 6 }, 43 }},
	{ 3400290142, { semver::version { 7, 5, 7 }, 43 }},
	{ 896212608, { semver::version { 7, 5, 8 }, 43 }},
	{ 1615407334, { semver::version { 7, 5, 9 }, 44 }},
	{ 2614338095, { semver::version { 7, 6, 0 }, 45 }},
	{ 821842784, { semver::version { 7, 6, 1 }, 46 }},
	{ 1663638946, { semver::version { 7, 6, 2 }, 47 }},
	{ 2560882250, { semver::version { 7, 6, 3 }, 47 }},
	{ 759094627, { semver::version { 7, 6, 4 }, 47 }},
	{ 1299418478, { semver::version { 7, 6, 5 }, 47 }},
	{ 3980399492, { semver::version { 7, 6, 6 }, 47 }},
	{ 2334287384, { semver::version { 7, 6, 7 }, 47 }},
	{ 1294762592, { semver::version { 7, 6, 8 }, 47 }},
	{ 1027589282, { semver::version { 7, 6, 9 }, 47 }},
	{ 4246675114, { semver::version { 7, 7, 0 }, 47 }},
	{ 359315047, { semver::version { 7, 7, 1 }, 47 }},
	{ 665729261, { semver::version { 7, 7, 2 }, 47 }},
	{ 570795461, { semver::version { 7, 7, 3 }, 47 }},
	{ 2453249279, { semver::version { 7, 7, 4 }, 47 }},
	{ 2519751597, { semver::version { 7, 7, 5 }, 47 }},
	{ 3456625766, { semver::version { 7, 7, 6 }, 47 }},
	{ 1610453906, { semver::version { 7, 7, 7 }, 47 }},
	{ 1141002876, { semver::version { 7, 7, 8 }, 47 }},
	{ 2078786867, { semver::version { 7, 7, 9 }, 47 }},
	{ 2452690866, { semver::version { 7, 8, 0 }, 47 }},
	{ 1496842040, { semver::version { 7, 8, 1 }, 47 }},
	{ 2993642707, { semver::version { 7, 8, 2 }, 47 }},
	{ 1010167477, { semver::version { 7, 8, 3 }, 47 }},
	{ 822044576, { semver::version { 7, 8, 4 }, 47 }},
	{ 871150613, { semver::version { 7, 8, 5 }, 47 }},
	{ 2315211703, { semver::version { 7, 8, 6 }, 47 }},
	{ 3444574301, { semver::version { 7, 8, 7 }, 47 }},
	{ 1089732217, { semver::version { 7, 8, 8 }, 47 }},
	{ 3028559132, { semver::version { 7, 8, 9 }, 47 }},
	{ 2338297284, { semver::version { 7, 9, 0 }, 47 }},
	{ 2124146138, { semver::version { 7, 9, 1 }, 48 }},
	{ 3174270079, { semver::version { 7, 9, 2 }, 49 }},
	{ 1166567353, { semver::version { 7, 9, 3 }, 49 }},
	{ 3097628208, { semver::version { 7, 9, 4 }, 49 }},
	{ 1608350382, { semver::version { 7, 9, 5 }, 49 }},
	{ 931661769, { semver::version { 7, 9, 6 }, 49 }},
	{ 51578483, { semver::version { 7, 9, 7 }, 49 }},
	{ 4293497990, { semver::version { 7, 9, 8 }, 49 }},
	{ 1906191210, { semver::version { 7, 9, 9 }, 49 }},
	{ 2365447925, { semver::version { 8, 0, 0 }, 49 }},
	{ 3754469461, { semver::version { 8, 0, 1 }, 49 }},
	{ 1398017248, { semver::version { 8, 1, 0 }, 49 }},
	{ 3645788937, { semver::version { 8, 1, 1 }, 49 }},
	{ 390200130, { semver::version { 8, 1, 2 }, 49 }},
	{ 421851576, { semver::version { 8, 1, 3 }, 49 }},
	{ 346243621, { semver::version { 8, 1, 4 }, 49 }},
	{ 1360339976, { semver::version { 8, 1, 5 }, 49 }},
	{ 4054829225, { semver::version { 8, 1, 6 }, 49 }},
	{ 2116663599, { semver::version { 8, 1, 7 }, 49 }},
	{ 316729465, { semver::version { 8, 1, 8 }, 49 }},
	{ 596542505, { semver::version { 8, 1, 9 }, 50 }},
	{ 3655984992, { semver::version { 8, 2, 0 }, 50 }},
	{ 1990670513, { semver::version { 8, 2, 1 }, 51 }},
	{ 4200197487, { semver::version { 8, 2, 2 }, 52 }},
	{ 1401211626, { semver::version { 8, 2, 3 }, 53 }},
	{ 516271883, { semver::version { 8, 2, 4 }, 53 }},
	{ 3278579259, { semver::version { 8, 2, 5 }, 53 }},
	{ 2739098733, { semver::version { 8, 2, 6 }, 53 }},
	{ 954920465, { semver::version { 8, 2, 7 }, 53 }},
	{ 1261160416, { semver::version { 8, 2, 8 }, 53 }},
	{ 373406070, { semver::version { 8, 2, 9 }, 53 }},
	{ 4093860710, { semver::version { 8, 3, 0 }, 53 }},
	{ 671525850, { semver::version { 8, 3, 1 }, 53 }},
	{ 659907922, { semver::version { 8, 3, 2 }, 53 }},
	{ 4244291994, { semver::version { 8, 3, 3 }, 53 }},
	{ 1491821160, { semver::version { 8, 3, 4 }, 53 }},
	{ 555647326, { semver::version { 8, 3, 5 }, 53 }},
	{ 371130489, { semver::version { 8, 3, 6 }, 53 }},
	{ 2848274780, { semver::version { 8, 3, 7 }, 53 }},
	{ 3191421837, { semver::version { 8, 3, 8 }, 53 }},
	{ 1233912831, { semver::version { 8, 3, 9 }, 53 }},
	{ 2593319323, { semver::version { 8, 4, 0 }, 53 }},
	{ 4256219480, { semver::version { 8, 4, 1 }, 53 }},
	{ 3832752736, { semver::version { 8, 4, 2 }, 53 }},
	{ 1495279254, { semver::version { 8, 4, 4 }, 53 }},
	{ 4096618398, { semver::version { 8, 4, 5 }, 53 }},
	{ 1090027336, { semver::version { 8, 4, 6 }, 53 }},
	{ 2462018828, { semver::version { 8, 4, 7 }, 53 }},
	{ 3588101684, { semver::version { 8, 4, 8 }, 53 }},
	{ 3205307263, { semver::version { 8, 4, 9 }, 53 }},
	{ 565810973, { semver::version { 8, 5, 0 }, 53 }},
	{ 103097418, { semver::version { 8, 5, 1 }, 53, "stable-candidate" }},
	{ 3954799564, { semver::version { 8, 6, 0 }, 53 }},
	{ 2467461470, { semver::version { 8, 6, 1 }, 53 }},
	{ 1211778499, { semver::version { 8, 6, 2 }, 53 }},
	{ 79034434, { semver::version { 8, 6, 3 }, 53 }},
	{ 1114454039, { semver::version { 8, 6, 4 }, 53 }},
	{ 3456048985, { semver::version { 8, 6, 5 }, 53 }},
	{ 2697705347, { semver::version { 8, 6, 6 }, 53 }},
	{ 3851574399, { semver::version { 8, 6, 7 }, 53 }},
	{ 3741126063, { semver::version { 8, 6, 8 }, 54 }},
	{ 2944267602, { semver::version { 8, 6, 9 }, 54 }},
	{ 2858820641, { semver::version { 8, 7, 0 }, 54 }},
	{ 798209154, { semver::version { 8, 7, 1 }, 54 }},
	{ 3834065561, { semver::version { 8, 7, 2 }, 54 }},
	{ 1479234534, { semver::version { 8, 7, 3 }, 54 }},
	{ 77374204, { semver::version { 8, 7, 4 }, 55 }},
	{ 1524178494, { semver::version { 8, 7, 5 }, 55 }},
	{ 3923000499, { semver::version { 8, 7, 6 }, 55 }},
	{ 3454781552, { semver::version { 8, 7, 7 }, 56 }},
	{ 1353968424, { semver::version { 8, 8, 0 }, 56 }},
	{ 2118034239, { semver::version { 8, 8, 1 }, 56 }},
	{ 1509076427, { semver::version { 8, 8, 2 }, 56 }},
	{ 2744446936, { semver::version { 8, 8, 3 }, 56 }},
	{ 4098754794, { semver::version { 8, 8, 4 }, 56 }},
	{ 3049195029, { semver::version { 8, 8, 5 }, 56 }},
	{ 4279111454, { semver::version { 8, 8, 6 }, 56 }},
	{ 980223932, { semver::version { 8, 8, 7 }, 56 }},
	{ 524870304, { semver::version { 8, 8, 8 }, 56 }},
	{ 3834190409, { semver::version { 8, 8, 9 }, 56 }},
	{ 2132659488, { semver::version { 8, 9, 0 }, 56 }},
	{ 87425250, { semver::version { 8, 9, 1 }, 56 }},
	{ 3741037828, { semver::version { 8, 9, 2 }, 56 }},
	{ 4099846163, { semver::version { 8, 9, 3 }, 56 }},
	{ 3522603779, { semver::version { 8, 9, 4 }, 56 }},
	{ 1336631363, { semver::version { 8, 9, 5 }, 57 }},
	{ 2690920620, { semver::version { 8, 9, 6 }, 57 }},
	{ 2163895253, { semver::version { 9, 0, 0 }, 57 }},
	{ 3802797961, { semver::version { 9, 0, 1 }, 58, "serial-send-instr" }},
	{ 3535380624, { semver::version { 9, 0, 1 }, 58 }},
	{ 1754387170, { semver::version { 9, 1, 0 }, 59 }},
	{ 866810166, { semver::version { 9, 1, 1 }, 59 }},
	{ 1815072274, { semver::version { 9, 1, 2 }, 59 }},
	{ 442054565, { semver::version { 9, 1, 3 }, 59 }},
	{ 1742601147, { semver::version { 9, 1, 4 }, 59, "test-alt-cgb-envs" }},
	{ 3222527884, { semver::version { 9, 1, 4 }, 59 }},
	{ 1178626673, { semver::version { 9, 1, 5 }, 59 }},
	{ 1325767163, { semver::version { 9, 1, 6 }, 59 }},
	{ 3152897338, { semver::version { 9, 1, 7 }, 59 }},
	{ 53283718, { semver::version { 9, 1, 8 }, 59 }},
	{ 1539732371, { semver::version { 9, 1, 9 }, 59 }},
	{ 1731080956, { semver::version { 9, 1, 10 }, 59 }},
	{ 171650318, { semver::version { 9, 1, 11 }, 59 }},
	{ 1398908547, { semver::version { 9, 1, 12 }, 60, "arduinoboy-untested" }},
	{ 1090576658, { semver::version { 9, 1, 12 }, 61 }},
	{ 2720898633, { semver::version { 9, 2, 0 }, 62 }},
	{ 1140514238, { semver::version { 9, 2, 1 }, 62 }},
	{ 3816710476, { semver::version { 9, 2, 2 }, 62 }},
	{ 3198552388, { semver::version { 9, 2, 3 }, 62 }},
	{ 1253677480, { semver::version { 9, 2, 4 }, 62 }},
	{ 1119492988, { semver::version { 9, 2, 5 }, 62 }},
	{ 510760574, { semver::version { 9, 2, 6 }, 62 }},
	{ 3772876596, { semver::version { 9, 2, 7 }, 62 }},
	{ 2921122598, { semver::version { 9, 2, 8 }, 62 }},
	{ 2260907489, { semver::version { 9, 2, 9 }, 62 }},
	{ 1249687980, { semver::version { 9, 2, 10 }, 62 }},
};

}