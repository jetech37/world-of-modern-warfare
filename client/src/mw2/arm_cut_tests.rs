//! The first-person arms' cut edge, measured over every melee pose: where the kept arm geometry
//! ends (the ring round the shoulder where the arm is cut from the body) must never be on screen,
//! and the arm must never pass through the near plane, where it would be seen sliced open.
//!
//! The fixtures are measurements (`examples/wow_arm_cut.rs`): MW2's view arms at the idle of a
//! rifle, a pistol, a shotgun, a sniper rifle, a launcher and the riot shield (the arms a melee
//! weapon is held with: the gun's own, its surfaces hidden), and a human male's arm with the ring
//! of its kept triangles at the shoulder; an orc's ring, larger for its hand, is checked too.

use std::collections::HashMap;

use bevy::math::{Mat4, Vec3, Vec4};

use super::*;
use crate::hand_anim;
use crate::melee_pose::{self, Grip, Motion};
use crate::hand_anims;

/// One MW2 joint: its matrix's x, y and z columns and its translation.
type Joint = [[f32; 3]; 4];

/// MW2's view arms at the idle (eye frame, inches, Bevy axes): per gun, the right then the left
/// arm's shoulder, elbow, wrist, middle finger 0-2 and thumb 0-2.
#[allow(clippy::approx_constant)] // measurements, not constants
const IDLES: &[(&str, [Joint; 9], [Joint; 9])] = &[
    (
        "m4_mp",
        [
            [
                [-0.3542, 0.4193, 0.8359],
                [0.8985, 0.4003, 0.18],
                [-0.2591, 0.8149, -0.5185],
                [6.966, -6.396, 13.2],
            ],
            [
                [0.634, -0.4437, 0.6334],
                [0.4326, -0.4755, -0.766],
                [0.641, 0.7597, -0.1095],
                [11.25, -11.47, 3.095],
            ],
            [
                [-0.09, -0.0225, 0.9957],
                [0.0224, -0.9995, -0.0206],
                [0.9957, 0.0204, 0.0905],
                [3.914, -6.332, -4.235],
            ],
            [
                [0.871, 0.267, 0.4124],
                [0.4689, -0.2017, -0.8599],
                [-0.1464, 0.9424, -0.3008],
                [4.454, -6.573, -7.998],
            ],
            [
                [0.4785, -0.2154, -0.8513],
                [-0.861, -0.3055, -0.4067],
                [-0.1725, 0.9275, -0.3316],
                [2.752, -7.095, -8.803],
            ],
            [
                [-0.1939, -0.3751, -0.9065],
                [-0.9537, -0.1446, 0.2638],
                [-0.23, 0.9156, -0.3297],
                [2.054, -6.78, -7.56],
            ],
            [
                [0.6154, -0.2122, 0.7592],
                [-0.2475, -0.9664, -0.0695],
                [0.7484, -0.1452, -0.6472],
                [3.524, -5.366, -4.7],
            ],
            [
                [0.2973, 0.7532, 0.5868],
                [0.4322, -0.6542, 0.6207],
                [0.8514, 0.0691, -0.52],
                [2.179, -4.804, -6.295],
            ],
            [
                [-0.1078, 0.9529, -0.2836],
                [0.5454, 0.2952, 0.7845],
                [0.8312, -0.0701, -0.5515],
                [1.63, -6.066, -7.148],
            ],
        ],
        [
            [
                [-0.2467, -0.3341, -0.9097],
                [0.95, -0.2686, -0.159],
                [-0.1912, -0.9034, 0.3837],
                [-1.334, -7.325, 2.129],
            ],
            [
                [0.5323, 0.4957, -0.6863],
                [0.4455, 0.5253, 0.7249],
                [0.7199, -0.6916, 0.0588],
                [-4.318, -11.37, -8.872],
            ],
            [
                [0.1084, 0.0884, -0.9902],
                [0.2376, 0.9649, 0.1122],
                [0.9653, -0.2474, 0.0836],
                [1.841, -5.63, -16.81],
            ],
            [
                [0.9979, 0.0256, 0.0601],
                [-0.0568, -0.1151, 0.9917],
                [0.0323, -0.993, -0.1134],
                [1.987, -5.566, -20.62],
            ],
            [
                [-0.1664, -0.0958, 0.9814],
                [-0.986, 0.0241, -0.1648],
                [-0.0079, -0.9951, -0.0985],
                [3.937, -5.516, -20.5],
            ],
            [
                [-0.8451, -0.0248, 0.534],
                [-0.5318, 0.1419, -0.8349],
                [-0.0551, -0.9896, -0.1331],
                [3.694, -5.656, -19.07],
            ],
            [
                [0.7936, 0.2026, -0.5737],
                [-0.0771, 0.9688, 0.2355],
                [0.6035, -0.1427, 0.7845],
                [2.515, -4.761, -17.12],
            ],
            [
                [0.2178, -0.7353, -0.6418],
                [0.6892, 0.5815, -0.4324],
                [0.6911, -0.3481, 0.6334],
                [4.253, -4.235, -18.29],
            ],
            [
                [-0.4325, -0.8861, 0.1664],
                [0.5695, -0.4116, -0.7116],
                [0.699, -0.213, 0.6826],
                [4.641, -5.496, -19.23],
            ],
        ],
    ),
    (
        "usp_mp",
        [
            [
                [-0.3884, 0.7306, 0.5615],
                [0.9054, 0.1893, 0.38],
                [0.1714, 0.656, -0.735],
                [1.064, -0.9258, 7.249],
            ],
            [
                [0.2513, -0.3513, 0.9019],
                [0.4318, -0.7933, -0.4292],
                [0.8663, 0.4973, -0.0477],
                [5.762, -9.762, 0.4585],
            ],
            [
                [-0.2111, -0.4253, 0.8801],
                [0.3643, -0.8698, -0.3329],
                [0.907, 0.2504, 0.3386],
                [2.854, -5.697, -9.978],
            ],
            [
                [0.7416, 0.1915, 0.6429],
                [0.6336, 0.1151, -0.7651],
                [-0.2206, 0.9747, -0.036],
                [3.95, -4.325, -13.36],
            ],
            [
                [0.8655, 0.1773, -0.4685],
                [-0.4405, -0.1762, -0.8803],
                [-0.2386, 0.9683, -0.0744],
                [2.501, -4.699, -14.61],
            ],
            [
                [0.0783, -0.0892, -0.9929],
                [-0.9555, -0.2907, -0.0493],
                [-0.2842, 0.9527, -0.108],
                [1.237, -4.958, -13.93],
            ],
            [
                [0.4837, -0.6006, 0.6367],
                [0.1125, -0.6787, -0.7257],
                [0.868, 0.4226, -0.2607],
                [2.225, -4.77, -10.19],
            ],
            [
                [-0.0519, 0.0956, 0.9941],
                [0.4997, -0.8594, 0.1088],
                [0.8647, 0.5023, -0.0032],
                [1.122, -3.429, -11.48],
            ],
            [
                [-0.2679, 0.736, 0.6217],
                [0.3598, -0.5222, 0.7732],
                [0.8937, 0.4309, -0.1249],
                [1.139, -3.691, -13.08],
            ],
        ],
        [
            [
                [0.0071, -0.7454, -0.6666],
                [0.9915, 0.092, -0.0924],
                [0.1302, -0.6602, 0.7397],
                [-4.099, -4.361, 6.047],
            ],
            [
                [0.3933, 0.593, -0.7026],
                [0.0044, 0.763, 0.6464],
                [0.9194, -0.2573, 0.2975],
                [-4.013, -13.38, -2.015],
            ],
            [
                [0.0443, 0.1299, -0.9905],
                [-0.3093, 0.9446, 0.1101],
                [0.9499, 0.3015, 0.082],
                [0.5385, -6.515, -10.14],
            ],
            [
                [0.7827, 0.3489, -0.5155],
                [0.428, 0.2997, 0.8526],
                [0.4519, -0.888, 0.0852],
                [0.6259, -6.392, -13.95],
            ],
            [
                [0.8789, 0.4436, 0.1753],
                [-0.2103, 0.0305, 0.9772],
                [0.4281, -0.8957, 0.1201],
                [2.155, -5.711, -14.96],
            ],
            [
                [0.6122, 0.3924, 0.6864],
                [-0.6861, -0.1679, 0.7079],
                [0.393, -0.9043, 0.1665],
                [3.439, -5.063, -14.7],
            ],
            [
                [-0.1972, 0.5352, -0.8214],
                [-0.7094, 0.5004, 0.4964],
                [0.6766, 0.6806, 0.281],
                [0.6499, -5.421, -10.45],
            ],
            [
                [0.1268, -0.0759, -0.989],
                [-0.5855, 0.7991, -0.1364],
                [0.8007, 0.5964, 0.0569],
                [0.191, -4.178, -12.16],
            ],
            [
                [0.5029, -0.3596, -0.786],
                [-0.4479, 0.6694, -0.5928],
                [0.7393, 0.6501, 0.1756],
                [0.5204, -4.282, -13.74],
            ],
        ],
    ),
    (
        "spas12_mp",
        [
            [
                [-0.3279, 0.7141, 0.6185],
                [0.9439, 0.2204, 0.2459],
                [0.0393, 0.6644, -0.7463],
                [6.556, -3.187, 11.89],
            ],
            [
                [0.4221, -0.4056, 0.8108],
                [0.3598, -0.7459, -0.5605],
                [0.8321, 0.5283, -0.1689],
                [10.52, -11.82, 4.407],
            ],
            [
                [0.0024, -0.0576, 0.9983],
                [-0.0083, -0.9983, -0.0576],
                [1.0, -0.0081, -0.0028],
                [5.638, -7.13, -4.975],
            ],
            [
                [0.8571, -0.0332, 0.514],
                [0.5146, 0.012, -0.8574],
                [0.0223, 0.9994, 0.0273],
                [5.819, -7.243, -8.778],
            ],
            [
                [0.9121, -0.0008, -0.4101],
                [-0.41, -0.008, -0.912],
                [-0.0025, 1.0, -0.0076],
                [4.144, -7.178, -9.782],
            ],
            [
                [-0.2624, -0.0557, -0.9633],
                [-0.9637, -0.0349, 0.2645],
                [-0.0483, 0.9978, -0.0445],
                [2.812, -7.177, -9.183],
            ],
            [
                [0.8691, -0.107, 0.4829],
                [0.126, -0.8962, -0.4254],
                [0.4783, 0.4306, -0.7654],
                [5.234, -6.138, -5.367],
            ],
            [
                [0.4264, 0.3963, 0.8131],
                [0.733, -0.6781, -0.0539],
                [0.53, 0.619, -0.5796],
                [3.32, -5.841, -6.324],
            ],
            [
                [-0.5527, 0.8095, 0.198],
                [0.6549, 0.275, 0.7039],
                [0.5154, 0.5187, -0.6821],
                [2.615, -6.59, -7.575],
            ],
        ],
        [
            [
                [0.4469, -0.5311, -0.7198],
                [0.8513, 0.4997, 0.1599],
                [0.2748, -0.6842, 0.6755],
                [-7.937, -5.518, -2.818],
            ],
            [
                [0.3959, 0.4606, -0.7945],
                [-0.2864, 0.8839, 0.3697],
                [0.8725, 0.0812, 0.4818],
                [-2.532, -11.94, -11.52],
            ],
            [
                [0.2552, -0.1445, -0.956],
                [-0.7546, 0.5885, -0.2903],
                [0.6046, 0.7955, 0.0411],
                [2.049, -6.613, -20.72],
            ],
            [
                [0.7352, 0.3112, -0.6022],
                [0.0529, 0.8594, 0.5086],
                [0.6757, -0.4058, 0.6154],
                [3.149, -7.508, -24.25],
            ],
            [
                [0.5333, 0.8459, -0.0066],
                [-0.5448, 0.3494, 0.7623],
                [0.6472, -0.4029, 0.6472],
                [4.586, -6.9, -25.43],
            ],
            [
                [0.0731, 0.885, 0.4598],
                [-0.7941, -0.2273, 0.5637],
                [0.6034, -0.4064, 0.6862],
                [5.364, -5.665, -25.44],
            ],
            [
                [0.0641, 0.4485, -0.8915],
                [-0.9201, 0.3724, 0.1212],
                [0.3863, 0.8125, 0.4366],
                [1.684, -5.783, -21.41],
            ],
            [
                [0.3605, 0.0701, -0.9301],
                [-0.7127, 0.664, -0.2262],
                [0.6018, 0.7444, 0.2893],
                [1.752, -4.733, -23.3],
            ],
            [
                [-0.0492, 0.4792, -0.8763],
                [-0.8622, 0.4225, 0.2795],
                [0.5042, 0.7693, 0.3924],
                [2.444, -4.576, -24.75],
            ],
        ],
    ),
    (
        "cheytac_mp",
        [
            [
                [-0.4116, 0.9061, 0.0974],
                [0.9109, 0.4057, 0.0756],
                [0.029, 0.1199, -0.9924],
                [7.937, -3.187, 1.998],
            ],
            [
                [0.5991, -0.4685, 0.6493],
                [0.0599, -0.7825, -0.6198],
                [0.7984, 0.4102, -0.4407],
                [12.92, -14.15, 0.8193],
            ],
            [
                [0.0016, -0.1597, 0.9872],
                [-0.0084, -0.9871, -0.1597],
                [1.0, -0.0081, -0.0029],
                [5.983, -8.725, -6.694],
            ],
            [
                [0.9397, 0.0493, 0.3386],
                [0.3399, -0.2465, -0.9076],
                [0.0387, 0.9679, -0.2484],
                [6.167, -8.448, -10.49],
            ],
            [
                [0.7219, -0.197, -0.6633],
                [-0.6919, -0.195, -0.6951],
                [0.0077, 0.9608, -0.2771],
                [4.331, -8.544, -11.15],
            ],
            [
                [-0.2187, -0.2985, -0.929],
                [-0.9746, 0.0201, 0.223],
                [-0.0479, 0.9542, -0.2954],
                [3.276, -8.256, -10.18],
            ],
            [
                [0.6915, -0.5431, 0.4762],
                [-0.3737, -0.8332, -0.4076],
                [0.6182, 0.1039, -0.7791],
                [5.58, -7.697, -6.983],
            ],
            [
                [0.5136, 0.3337, 0.7905],
                [0.3885, -0.9119, 0.1325],
                [0.7651, 0.239, -0.5979],
                [4.091, -6.448, -7.927],
            ],
            [
                [0.1933, 0.9041, 0.3812],
                [0.6717, -0.4051, 0.6202],
                [0.7151, 0.1362, -0.6856],
                [3.199, -7.067, -9.128],
            ],
        ],
        [
            [
                [-0.0424, -0.672, -0.7394],
                [0.9782, -0.1784, 0.1061],
                [-0.2032, -0.7188, 0.6649],
                [-4.453, -5.684, -0.3849],
            ],
            [
                [0.6424, 0.3461, -0.6838],
                [0.2066, 0.781, 0.5894],
                [0.738, -0.5198, 0.4302],
                [-4.965, -13.81, -9.327],
            ],
            [
                [0.351, 0.6154, -0.7057],
                [-0.619, 0.718, 0.3183],
                [0.7026, 0.3251, 0.633],
                [2.468, -9.806, -17.24],
            ],
            [
                [0.8795, 0.4482, 0.1601],
                [-0.2475, 0.1433, 0.9582],
                [0.4065, -0.8824, 0.237],
                [3.868, -7.773, -20.14],
            ],
            [
                [0.2114, 0.3391, 0.9167],
                [-0.9058, -0.2845, 0.3141],
                [0.3673, -0.8967, 0.247],
                [5.586, -6.898, -19.83],
            ],
            [
                [-0.3876, 0.1056, 0.9157],
                [-0.8668, -0.3797, -0.3231],
                [0.3136, -0.919, 0.2387],
                [5.895, -6.402, -18.49],
            ],
            [
                [0.1222, 0.8571, -0.5004],
                [-0.9554, 0.2382, 0.1746],
                [0.2688, 0.4567, 0.848],
                [2.317, -8.703, -16.99],
            ],
            [
                [0.3037, 0.6793, -0.6681],
                [-0.8088, 0.5545, 0.1961],
                [0.5036, 0.4808, 0.7178],
                [2.5, -6.804, -18.0],
            ],
            [
                [-0.0023, 0.8868, -0.4622],
                [-0.9128, 0.1869, 0.3632],
                [0.4085, 0.4228, 0.809],
                [3.095, -5.686, -19.01],
            ],
        ],
    ),
    (
        "rpg_mp",
        [
            [
                [0.2712, 0.73, -0.6274],
                [0.9469, -0.3194, 0.0377],
                [-0.1728, -0.6043, -0.7778],
                [7.938, -1.81, -1.126],
            ],
            [
                [0.2764, -0.2372, 0.9313],
                [-0.629, -0.7773, -0.0113],
                [0.7266, -0.5827, -0.3641],
                [4.658, -10.64, 6.461],
            ],
            [
                [0.002, -0.1066, 0.9943],
                [-0.0083, -0.9943, -0.1065],
                [1.0, -0.008, -0.0029],
                [1.461, -7.893, -4.315],
            ],
            [
                [0.6002, -0.0495, 0.7983],
                [0.7998, 0.036, -0.5991],
                [0.0009, 0.9981, 0.0613],
                [1.643, -7.82, -8.119],
            ],
            [
                [0.9987, 0.008, 0.0494],
                [0.0493, 0.0199, -0.9986],
                [-0.0089, 0.9998, 0.0194],
                [0.4702, -7.723, -9.679],
            ],
            [
                [0.8588, 0.0107, -0.5122],
                [-0.5112, -0.0462, -0.8582],
                [-0.0329, 0.9989, -0.0342],
                [-0.9886, -7.735, -9.751],
            ],
            [
                [0.4616, -0.6484, 0.6054],
                [-0.3727, -0.761, -0.531],
                [0.805, 0.0195, -0.5929],
                [1.057, -6.882, -4.658],
            ],
            [
                [0.3836, -0.0614, 0.9214],
                [0.0952, -0.9898, -0.1056],
                [0.9186, 0.1282, -0.3739],
                [0.0536, -5.408, -5.879],
            ],
            [
                [0.4239, 0.4212, 0.8018],
                [0.267, -0.9041, 0.3337],
                [0.8655, 0.0727, -0.4957],
                [-0.6628, -5.383, -7.331],
            ],
        ],
        [
            [
                [0.1202, -0.8217, -0.5571],
                [0.9894, 0.1454, -0.001],
                [0.0818, -0.5511, 0.8304],
                [-7.937, -1.81, 5.752],
            ],
            [
                [0.451, 0.3912, -0.8023],
                [-0.0402, 0.9068, 0.4196],
                [0.8916, -0.157, 0.4247],
                [-6.568, -11.81, -1.067],
            ],
            [
                [0.0013, 0.1753, -0.9845],
                [-0.0085, 0.9845, 0.1753],
                [1.0, 0.0081, 0.0028],
                [-1.35, -7.285, -10.35],
            ],
            [
                [0.8846, -0.1382, -0.4454],
                [0.4506, 0.0072, 0.8927],
                [-0.1202, -0.9904, 0.0687],
                [-1.534, -6.948, -14.14],
            ],
            [
                [0.6448, -0.0176, 0.7642],
                [-0.7501, 0.1778, 0.637],
                [-0.1471, -0.9839, 0.1015],
                [0.1938, -7.218, -15.01],
            ],
            [
                [-0.1605, 0.1483, 0.9758],
                [-0.9659, 0.1799, -0.1862],
                [-0.2032, -0.9724, 0.1144],
                [1.136, -7.243, -13.89],
            ],
            [
                [0.5196, 0.3994, -0.7553],
                [-0.0106, 0.8869, 0.4618],
                [0.8543, -0.232, 0.4651],
                [-0.9469, -6.253, -10.62],
            ],
            [
                [0.2048, -0.1544, -0.9665],
                [0.4174, 0.907, -0.0564],
                [0.8853, -0.3919, 0.2502],
                [0.2208, -5.315, -12.18],
            ],
            [
                [0.114, -0.6098, -0.7843],
                [0.4689, 0.729, -0.4987],
                [0.8759, -0.311, 0.369],
                [0.6257, -5.662, -13.71],
            ],
        ],
    ),
    (
        "riotshield_mp",
        [
            [
                [-0.1609, 0.9147, -0.3707],
                [0.8802, 0.3029, 0.3654],
                [0.4465, -0.2675, -0.8538],
                [5.133, -7.354, 3.506],
            ],
            [
                [-0.083, 0.95, 0.301],
                [0.1522, -0.2864, 0.946],
                [0.9849, 0.1243, -0.1208],
                [7.079, -18.42, 7.99],
            ],
            [
                [-0.7221, 0.5937, 0.355],
                [-0.0017, -0.5147, 0.8573],
                [0.6917, 0.6185, 0.3727],
                [8.039, -29.41, 4.508],
            ],
            [
                [-0.1639, 0.8662, 0.4721],
                [0.986, 0.1295, 0.1047],
                [0.0295, 0.4827, -0.8753],
                [10.91, -31.71, 3.518],
            ],
            [
                [0.6544, 0.6585, 0.3716],
                [0.7543, -0.6029, -0.2599],
                [0.0529, 0.4504, -0.8912],
                [11.23, -33.4, 2.595],
            ],
            [
                [0.9623, 0.2111, 0.1715],
                [0.2615, -0.8916, -0.3696],
                [0.0749, 0.4005, -0.9132],
                [10.27, -34.36, 2.053],
            ],
            [
                [-0.1646, 0.39, 0.906],
                [0.0693, -0.9117, 0.405],
                [0.9839, 0.1295, 0.1231],
                [8.082, -29.43, 3.368],
            ],
            [
                [-0.403, 0.805, 0.4354],
                [0.0693, -0.4476, 0.8916],
                [0.9126, 0.3895, 0.1246],
                [8.375, -30.19, 1.366],
            ],
            [
                [-0.2537, 0.9672, -0.0121],
                [-0.1219, -0.0196, 0.9923],
                [0.9596, 0.2533, 0.1229],
                [8.928, -31.56, 0.7019],
            ],
        ],
        [
            [
                [-0.3285, -0.0574, -0.9428],
                [0.6032, -0.7809, -0.1626],
                [-0.7268, -0.6221, 0.2911],
                [-5.134, -7.354, 3.506],
            ],
            [
                [0.9367, 0.1379, -0.322],
                [0.3172, 0.0558, 0.9467],
                [0.1485, -0.9889, 0.0085],
                [-9.107, -8.048, -7.896],
            ],
            [
                [0.8836, 0.4426, 0.1528],
                [-0.4582, 0.8846, 0.087],
                [-0.0966, -0.1468, 0.9844],
                [1.732, -6.452, -11.62],
            ],
            [
                [0.1314, -0.0448, 0.9903],
                [-0.9807, -0.1515, 0.1233],
                [0.1445, -0.9874, -0.0638],
                [5.251, -5.039, -11.26],
            ],
            [
                [-0.8156, -0.1577, 0.5568],
                [-0.5656, 0.0138, -0.8246],
                [0.1224, -0.9874, -0.1005],
                [5.508, -5.126, -9.326],
            ],
            [
                [-0.9762, -0.0812, -0.201],
                [0.1843, 0.1776, -0.9667],
                [0.1142, -0.9807, -0.1584],
                [4.316, -5.357, -8.513],
            ],
            [
                [0.2642, 0.6237, 0.7357],
                [-0.4693, 0.7495, -0.4669],
                [-0.8426, -0.2219, 0.4907],
                [1.647, -5.46, -11.06],
            ],
            [
                [0.7435, -0.3142, 0.5903],
                [0.0196, 0.8926, 0.4503],
                [-0.6684, -0.3232, 0.6699],
                [2.124, -4.051, -9.496],
            ],
            [
                [0.4275, -0.8802, 0.2062],
                [0.4951, 0.4188, 0.7612],
                [-0.7564, -0.2233, 0.6148],
                [3.248, -4.648, -8.495],
            ],
        ],
    ),
];

/// The ring where a human male's kept arm triangles meet the dropped body (right arm, bind model
/// units, relative to the upper arm's pivot; `examples/wow_arm_cut.rs`, "drawn today").
const HUMAN_RING: [[f32; 3]; 7] = [
    [-0.029, -0.013, -0.117],
    [-0.040, 0.045, -0.084],
    [-0.059, -0.050, 0.111],
    [-0.017, -0.102, -0.113],
    [-0.055, 0.036, 0.071],
    [-0.051, 0.079, -0.016],
    [-0.006, -0.101, -0.015],
];
/// An orc male's ring (its own units: scaled to the human's hand by [`ORC_TO_HUMAN`]).
const ORC_RING: [[f32; 3]; 9] = [
    [-0.041, -0.230, 0.121],
    [-0.046, -0.062, 0.152],
    [-0.046, 0.025, -0.109],
    [-0.046, 0.075, -0.001],
    [-0.025, -0.233, -0.001],
    [-0.046, -0.146, -0.238],
    [-0.046, -0.045, -0.184],
    [-0.046, -0.245, -0.212],
    [-0.046, 0.032, 0.101],
];
/// Orc hand reach 0.304, human 0.214.
const ORC_TO_HUMAN: f32 = 0.214 / 0.304;
/// The human male's upper arm and forearm girth (bind units, the widest vertex off the bone).
const UPPER_GIRTH: f32 = 0.136;
const FORE_GIRTH: f32 = 0.120;
/// The human male's hand reach (bind units), what `arm_scale` matches to MW2's hand.
const HUMAN_REACH: f32 = 0.214;

/// The view: the stock near plane (0.1 yd), the stock vertical field (45 degrees) and the widest
/// screen (21:9), each widened for the weapon's sway and bob, which move the whole view model by
/// an inch or two and a few degrees.
const NEAR: f32 = crate::fp_body::FP_NEARCLIP * 36.0;
const SWAY_INCHES: f32 = 1.0;
const SWAY_DEGREES: f32 = 2.0;

fn tan_half_v() -> f32 {
    (22.5f32 + SWAY_DEGREES).to_radians().tan()
}

fn tan_half_h() -> f32 {
    tan_half_h_at(21.0 / 9.0)
}

/// The horizontal half-field's tangent on a screen `aspect` wide, widened for the sway.
fn tan_half_h_at(aspect: f32) -> f32 {
    let wide = (22.5f32.to_radians().tan() * aspect).atan();
    (wide + SWAY_DEGREES.to_radians()).tan()
}

/// A point the camera sees: in front of the near plane and inside the (widened) frustum.
fn seen(p: Vec3) -> bool {
    seen_past(p, NEAR, tan_half_h())
}

/// [`seen`] with the near plane at `near` (inches) and the horizontal half-field `tan_h`.
fn seen_past(p: Vec3, near: f32, tan_h: f32) -> bool {
    let depth = -p.z;
    depth > near - SWAY_INCHES
        && p.x.abs() - SWAY_INCHES < depth * tan_h
        && p.y.abs() - SWAY_INCHES < depth * tan_half_v()
}

/// Whether a segment `girth` thick passes through the near plane's window, where it would be seen
/// sliced open.
fn through_near_window(a: Vec3, b: Vec3, girth: f32) -> bool {
    let (hw, hh) = (
        NEAR * tan_half_h() + SWAY_INCHES,
        NEAR * tan_half_v() + SWAY_INCHES,
    );
    (0..=64).any(|i| {
        let p = a.lerp(b, i as f32 / 64.0);
        let q = Vec3::new(p.x.clamp(-hw, hw), p.y.clamp(-hh, hh), -NEAR);
        p.distance(q) < girth
    })
}

/// The idle skeleton: bones 0-8 the right arm, 9-17 the left, chained as MW2's view arms are.
fn idle_world(right: &[Joint; 9], left: &[Joint; 9]) -> (Vec<Mat4>, Vec<Option<usize>>) {
    let mut world = Vec::new();
    let mut parents = Vec::new();
    for (side, joints) in [right, left].into_iter().enumerate() {
        let b = side * 9;
        for j in joints {
            let col = |c: [f32; 3], w: f32| Vec4::new(c[0], c[1], c[2], w);
            world.push(Mat4::from_cols(
                col(j[0], 0.0),
                col(j[1], 0.0),
                col(j[2], 0.0),
                col(j[3], 1.0),
            ));
        }
        parents.extend([
            None,
            Some(b),
            Some(b + 1),
            Some(b + 2),
            Some(b + 3),
            Some(b + 4),
            Some(b + 2),
            Some(b + 6),
            Some(b + 7),
        ]);
    }
    (world, parents)
}

const ARMS: [Option<[usize; 9]>; 2] = [
    Some([0, 1, 2, 3, 4, 5, 6, 7, 8]),
    Some([9, 10, 11, 12, 13, 14, 15, 16, 17]),
];

/// Every pose the melee weapon, a spell or a shield puts the hands in: each of the tracks' clips
/// through its length, at the ready and carried low for a sprint, with the off hand on its clip or
/// hanging empty; and the hand-made keys.
fn poses() -> Vec<(String, Grip, Option<Grip>)> {
    let mut out = Vec::new();
    for name in hand_anims::NAMES {
        // A build without the tracks (private-assets.md) checks the hand-made keys alone.
        let Some(track) = hand_anims::load(name) else {
            continue;
        };
        for i in 0..=24 {
            let k = i as f32 / 24.0;
            let (r, l) = hand_anim::at(&track, k);
            let low = |g: &Grip| melee_pose::lowered(g);
            out.push((format!("{name} {k:.2}"), r, Some(l)));
            out.push((format!("{name} {k:.2} low"), low(&r), Some(low(&l))));
            out.push((format!("{name} {k:.2} right only"), r, None));
        }
    }
    for two in [false, true] {
        for m in [
            Motion::Ready,
            Motion::Block,
            Motion::Power,
            Motion::Swing { from_right: true },
            Motion::Swing { from_right: false },
        ] {
            for i in 0..=20 {
                let k = i as f32 / 20.0;
                let g = melee_pose::grip(m, two, k);
                let off = Grip {
                    at: melee_pose::off_hand(&g, two, 4.5),
                    ..g
                };
                out.push((format!("hand-made {m:?} two {two} {k:.2}"), g, Some(off)));
                out.push((
                    format!("hand-made {m:?} two {two} {k:.2} mirrored"),
                    g,
                    Some(melee_pose::mirrored(&g)),
                ));
                out.push((
                    format!("hand-made {m:?} two {two} {k:.2} drawn"),
                    melee_pose::drawn(&g, k),
                    None,
                ));
            }
        }
    }
    for i in 0..=20 {
        let k = i as f32 / 20.0;
        let s = melee_pose::spell_grip(Some(k));
        out.push((format!("spell {k:.2}"), s, Some(melee_pose::mirrored(&s))));
        let ready = melee_pose::grip(Motion::Ready, false, 0.0);
        out.push((
            format!("shield {k:.2}"),
            ready,
            Some(melee_pose::shield_grip(k)),
        ));
    }
    out
}

/// The human male's right arm (the `vanilla_hand` fixture), or its mirror for the left.
fn chain(left: bool) -> (ArmChain, Vec<Vec3>) {
    let (parents, mut pivots, masks) = tests::vanilla_hand();
    if left {
        for p in &mut pivots {
            p.x = -p.x;
        }
    }
    let mut c = arm_chain(&parents, &pivots, &masks, 3).expect("a hand");
    let mut thumb_way = Vec3::new(-0.023, -0.088, -0.035).normalize();
    if left {
        thumb_way.x = -thumb_way.x;
    }
    c.dirs.insert(3, thumb_way);
    (c, pivots)
}

/// Pose both arms for every gun and pose as the view model does (`hold_melee`, then
/// `wow_arms::skin`), handing each arm's bones to `f`.
fn each_posed(mut f: impl FnMut(&str, usize, &HashMap<usize, Mat4>, &ArmChain, &[Vec3], f32)) {
    let chains = [chain(false), chain(true)];
    let poses = poses();
    for (gun, right, left) in IDLES {
        let (idle, parents) = idle_world(right, left);
        for (pose, r, l) in &poses {
            let mut world = idle.clone();
            let (mut r, mut l) = (*r, *l);
            crate::mw2::viewmodel::hold_melee(
                &mut world,
                &parents,
                ARMS,
                Mat4::IDENTITY,
                &mut r,
                l.as_mut(),
                &crate::hand_anim::Hold {
                    two_handed: false,
                    wrists: [
                        crate::hand_anim::wrist::WEAPON,
                        crate::hand_anim::wrist::LEFT,
                    ],
                    free: [false, true],
                },
            );
            let shapes = [Some(r.hand), l.map(|l| l.hand)];
            for (side, (chain, pivots)) in chains.iter().enumerate() {
                let j = ARMS[side].expect("both arms");
                let p: Vec<Vec3> = j.iter().map(|&b| world[b].w_axis.truncate()).collect();
                let mut target = ArmTarget {
                    shoulder: p[0],
                    elbow: p[1],
                    wrist: p[2],
                    mid: [p[3], p[4], p[5]],
                    thumb: [p[6], p[7], p[8]],
                };
                let k = arm_scale(chain, pivots, &target);
                if let Some(shape) = shapes[side] {
                    (target.mid, target.thumb) = shape.place(p[2], p[3], p[6]);
                }
                let closed = shapes[side].map_or(0.0, |s| s.closed());
                let bones = arm_bones(chain, pivots, &target, k, closed);
                f(
                    &format!("{gun} {pose} side {side}"),
                    side,
                    &bones,
                    chain,
                    pivots,
                    k,
                );
            }
        }
    }
}

/// What is wrong with the drawn arms in a posed skeleton `world` (bones 0-8 the right arm, 9-17
/// the left, eye frame) with each hand's shape: the cut ring on screen, an arm through the near
/// plane, as [`the_arms_cut_edge_is_never_on_screen`] and
/// [`the_arm_never_passes_through_the_near_plane`] measure them.
pub(super) fn arm_faults(
    world: &[Mat4],
    shapes: [Option<crate::hand_shape::HandShape>; 2],
) -> Vec<String> {
    thread_local! {
        static CHAINS: [(ArmChain, Vec<Vec3>); 2] = [chain(false), chain(true)];
    }
    let mut out = Vec::new();
    CHAINS.with(|chains| {
        for (side, (chain, pivots)) in chains.iter().enumerate() {
            let j = ARMS[side].expect("both arms");
            let p: Vec<Vec3> = j.iter().map(|&b| world[b].w_axis.truncate()).collect();
            let mut target = ArmTarget {
                shoulder: p[0],
                elbow: p[1],
                wrist: p[2],
                mid: [p[3], p[4], p[5]],
                thumb: [p[6], p[7], p[8]],
            };
            let k = arm_scale(chain, pivots, &target);
            if let Some(shape) = shapes[side] {
                (target.mid, target.thumb) = shape.place(p[2], p[3], p[6]);
            }
            let closed = shapes[side].map_or(0.0, |s| s.closed());
            let bones = arm_bones(chain, pivots, &target, k, closed);
            let upper = bones[&chain.upper];
            for r in
                ring(side, &HUMAN_RING, 1.0)
                    .into_iter()
                    .chain(ring(side, &ORC_RING, ORC_TO_HUMAN))
            {
                let q = upper.transform_point3(pivots[chain.upper] + r);
                if seen(q) {
                    out.push(format!("side {side}: ring on screen at {q:.1}"));
                }
            }
            let at = |b: usize, p: usize| bones[&b].transform_point3(pivots[p]);
            let shoulder = at(chain.upper, chain.upper);
            let elbow = at(chain.forearm, chain.forearm);
            let wrist = at(chain.hand, chain.hand);
            let girth = crate::mw2::viewmodel::ARM_GIRTH_PER_HAND * HUMAN_REACH * k;
            if through_near_window(shoulder, elbow, girth.max(UPPER_GIRTH * k)) {
                out.push(format!("side {side}: upper arm through the near plane"));
            }
            if through_near_window(elbow, wrist, girth.max(FORE_GIRTH * k)) {
                out.push(format!("side {side}: forearm through the near plane"));
            }
        }
    });
    out
}

/// A ring for `side` (the left mirrored), scaled into the human arm's units.
fn ring(side: usize, ring: &[[f32; 3]], scale: f32) -> Vec<Vec3> {
    let m = if side == 1 { -1.0 } else { 1.0 };
    ring.iter()
        .map(|r| Vec3::new(r[0] * m, r[1], r[2]) * scale)
        .collect()
}

#[test]
fn the_arms_cut_edge_is_never_on_screen() {
    let mut fails: Vec<(f32, String)> = Vec::new();
    let mut checked = 0;
    each_posed(|what, side, bones, chain, pivots, _k| {
        let upper = bones[&chain.upper];
        for (label, r) in [
            ("human", ring(side, &HUMAN_RING, 1.0)),
            ("orc", ring(side, &ORC_RING, ORC_TO_HUMAN)),
        ] {
            for p in r {
                checked += 1;
                let q = upper.transform_point3(pivots[chain.upper] + p);
                if seen(q) {
                    fails.push((-q.z, format!("{what} ({label} ring at {q:.1})")));
                }
            }
        }
    });
    fails.sort_by(|a, b| b.0.total_cmp(&a.0));
    let mut by: std::collections::BTreeMap<String, usize> = Default::default();
    for (_, w) in &fails {
        let mut it = w.split(' ');
        let gun = it.next().unwrap_or_default().to_string();
        let side = w
            .split("side ")
            .nth(1)
            .and_then(|r| r.split(' ').next())
            .unwrap_or_default();
        let ring = if w.contains("orc ring") {
            "orc"
        } else {
            "human"
        };
        *by.entry(format!("{gun} side {side} {ring}")).or_default() += 1;
    }
    if !by.is_empty() {
        eprintln!("ring failures by gun: {by:#?}");
    }
    assert!(
        fails.is_empty(),
        "{} of {checked} ring points on screen, e.g. {:#?}",
        fails.len(),
        fails.iter().take(12).map(|f| &f.1).collect::<Vec<_>>()
    );
}

#[test]
fn the_arm_never_passes_through_the_near_plane() {
    let mut fails = Vec::new();
    each_posed(|what, _side, bones, chain, pivots, k| {
        let at = |b: usize, p: usize| bones[&b].transform_point3(pivots[p]);
        let shoulder = at(chain.upper, chain.upper);
        let elbow = at(chain.forearm, chain.forearm);
        let wrist = at(chain.hand, chain.hand);
        // The widest race's girth for this hand (an orc's), not the human fixture's.
        let girth = crate::mw2::viewmodel::ARM_GIRTH_PER_HAND * HUMAN_REACH * k;
        if through_near_window(shoulder, elbow, girth.max(UPPER_GIRTH * k)) {
            fails.push(format!("{what}: upper arm {shoulder:.1} to {elbow:.1}"));
        }
        if through_near_window(elbow, wrist, girth.max(FORE_GIRTH * k)) {
            fails.push(format!("{what}: forearm {elbow:.1} to {wrist:.1}"));
        }
    });
    let mut by: std::collections::BTreeMap<String, usize> = Default::default();
    for w in &fails {
        let gun = w.split(' ').next().unwrap_or_default().to_string();
        let side = w
            .split("side ")
            .nth(1)
            .and_then(|r| r.split(':').next())
            .unwrap_or_default();
        let part = if w.contains("upper arm") {
            "upper"
        } else {
            "fore"
        };
        *by.entry(format!("{gun} side {side} {part}")).or_default() += 1;
    }
    if !by.is_empty() {
        eprintln!("near failures by gun: {by:#?}");
    }
    assert!(
        fails.is_empty(),
        "{} poses slice an arm, e.g. {:#?}",
        fails.len(),
        fails.iter().take(12).collect::<Vec<_>>()
    );
}

#[test]
fn the_arm_is_whole_at_its_joints_in_every_pose() {
    each_posed(|what, _side, bones, chain, pivots, _k| {
        let at = |b: usize, p: usize| bones[&b].transform_point3(pivots[p]);
        // The upper arm's far end is the forearm's root, the forearm's the hand's.
        let elbow_gap = at(chain.upper, chain.forearm).distance(at(chain.forearm, chain.forearm));
        let wrist_gap = at(chain.forearm, chain.hand).distance(at(chain.hand, chain.hand));
        assert!(
            elbow_gap < 1e-2 && wrist_gap < 1e-2,
            "{what}: {elbow_gap} {wrist_gap}"
        );
    });
}

/// With a gun up the arms are MW2's own clips (no hold), drawn with the stock near plane: at each
/// gun's idle the cut edge sits past MW2's shoulder, where MW2's own view arms end, off a 16:9
/// screen (MW2's own; a pump shotgun's forward shoulder shows MW2's own arm's end on a wider one).
#[test]
fn at_each_guns_idle_the_cut_edge_is_off_screen() {
    const STOCK_NEAR: f32 = 0.1 * 36.0;
    let chains = [chain(false), chain(true)];
    let mut fails = Vec::new();
    for (gun, right, left) in IDLES {
        let (idle, _) = idle_world(right, left);
        for (side, (chain, pivots)) in chains.iter().enumerate() {
            let j = ARMS[side].expect("both arms");
            let p: Vec<Vec3> = j.iter().map(|&b| idle[b].w_axis.truncate()).collect();
            let target = ArmTarget {
                shoulder: p[0],
                elbow: p[1],
                wrist: p[2],
                mid: [p[3], p[4], p[5]],
                thumb: [p[6], p[7], p[8]],
            };
            let k = arm_scale(chain, pivots, &target);
            let bones = arm_bones(chain, pivots, &target, k, 0.0);
            let upper = bones[&chain.upper];
            for r in
                ring(side, &HUMAN_RING, 1.0)
                    .into_iter()
                    .chain(ring(side, &ORC_RING, ORC_TO_HUMAN))
            {
                let q = upper.transform_point3(pivots[chain.upper] + r);
                if seen_past(q, STOCK_NEAR, tan_half_h_at(16.0 / 9.0)) {
                    fails.push(format!("{gun} side {side}: ring at {q:.1}"));
                }
            }
        }
    }
    assert!(fails.is_empty(), "{fails:#?}");
}

#[path = "melee_anim_tests.rs"]
mod melee_anim;
