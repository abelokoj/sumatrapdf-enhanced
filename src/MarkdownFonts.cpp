/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "base/Base.h"
#include "base/Crypto.h"
#include "base/ByteReaderWriter.h"
#include "base/File.h"
#include "base/Http.h"
#include "Settings.h"
#include "AppSettings.h"
#include "AppTools.h"
#include "SumatraPDF.h"
#include "Translations.h"
#if IS_DEBUG
#include "base/tests/UtAssert.h"
#endif
#include "MarkdownFonts.h"

struct MarkdownFontFace {
    const char* file;
    int weight;
    const char* style;
};
struct MarkdownFontFamily {
    const char* name;
    const char* cssName;
    const MarkdownFontFace* faces;
    int count;
};
constexpr const char* kMarkdownFontHost = "https://sumatrapdf.markdown/_enhanced_fonts/";

static const MarkdownFontFace kLora[] = {
    {"Lora-Regular.otf", 400, "normal"},  {"Lora-Italic.otf", 400, "italic"},
    {"Lora-Medium.otf", 500, "normal"},   {"Lora-MediumItalic.otf", 500, "italic"},
    {"Lora-SemiBold.otf", 600, "normal"}, {"Lora-SemiBoldItalic.otf", 600, "italic"},
    {"Lora-Bold.otf", 700, "normal"},     {"Lora-BoldItalic.otf", 700, "italic"},
};

static const MarkdownFontFace kLibertinus[] = {
    {"LibertinusSerif-Regular.otf", 400, "normal"},  {"LibertinusSerif-Italic.otf", 400, "italic"},
    {"LibertinusSerif-Semibold.otf", 600, "normal"}, {"LibertinusSerif-SemiboldItalic.otf", 600, "italic"},
    {"LibertinusSerif-Bold.otf", 700, "normal"},     {"LibertinusSerif-BoldItalic.otf", 700, "italic"},
};

static const MarkdownFontFace kNewCM10[] = {
    {"EnhancedWebNewCM10-Regular.woff2", 400, "normal"},
    {"EnhancedWebNewCM10-Italic.woff2", 400, "italic"},
    {"EnhancedWebNewCM10-Bold.woff2", 700, "normal"},
    {"EnhancedWebNewCM10-BoldItalic.woff2", 700, "italic"},
};

static const MarkdownFontFace kNewCM10Book[] = {
    {"EnhancedWebNewCM10-Book.woff2", 400, "normal"},
    {"EnhancedWebNewCM10-BookItalic.woff2", 400, "italic"},
    {"EnhancedWebNewCM10-Bold.woff2", 700, "normal"},
    {"EnhancedWebNewCM10-BoldItalic.woff2", 700, "italic"},
};

static const MarkdownFontFace kNewCMSans10[] = {
    {"EnhancedWebNewCMSans10-Regular.woff2", 400, "normal"},
    {"EnhancedWebNewCMSans10-Oblique.woff2", 400, "italic"},
    {"EnhancedWebNewCMSans10-Bold.woff2", 700, "normal"},
    {"EnhancedWebNewCMSans10-BoldOblique.woff2", 700, "italic"},
};

static const MarkdownFontFace kNewCMSans10Book[] = {
    {"EnhancedWebNewCMSans10-Book.woff2", 400, "normal"},
    {"EnhancedWebNewCMSans10-BookOblique.woff2", 400, "italic"},
    {"EnhancedWebNewCMSans10-Bold.woff2", 700, "normal"},
    {"EnhancedWebNewCMSans10-BoldOblique.woff2", 700, "italic"},
};

static const MarkdownFontFace kNewCMMono10[] = {
    {"EnhancedWebNewCMMono10-Regular.woff2", 400, "normal"},
    {"EnhancedWebNewCMMono10-Italic.woff2", 400, "italic"},
    {"EnhancedWebNewCMMono10-Bold.woff2", 700, "normal"},
    {"EnhancedWebNewCMMono10-BoldOblique.woff2", 700, "italic"},
};

static const MarkdownFontFace kNewCMMono10Book[] = {
    {"EnhancedWebNewCMMono10-Book.woff2", 400, "normal"},
    {"EnhancedWebNewCMMono10-BookItalic.woff2", 400, "italic"},
    {"EnhancedWebNewCMMono10-Bold.woff2", 700, "normal"},
    {"EnhancedWebNewCMMono10-BoldOblique.woff2", 700, "italic"},
};

static const MarkdownFontFace kNewCMUncial10[] = {
    {"EnhancedWebNewCMUncial10-Regular.woff2", 400, "normal"},
    {"EnhancedWebNewCMUncial10-Bold.woff2", 700, "normal"},
};

static const MarkdownFontFace kLibertinusMath[] = {
    {"LibertinusMath-Regular.otf", 400, "normal"},
};

static const MarkdownFontFace kNewCMMath[] = {
    {"EnhancedWebNewCMMath-Regular.woff2", 400, "normal"},
    {"EnhancedWebNewCMMath-Bold.woff2", 700, "normal"},
};

static const MarkdownFontFace kNewCMMathBook[] = {
    {"EnhancedWebNewCMMath-Book.woff2", 400, "normal"},
    {"EnhancedWebNewCMMath-Bold.woff2", 700, "normal"},
};

static const MarkdownFontFace kNewCMSansMath[] = {
    {"EnhancedWebNewCMSansMath-Regular.woff2", 400, "normal"},
};

static const MarkdownFontFamily kMarkdownTextFonts[] = {
    {"", "", nullptr, 0},
    {"Lora", "EnhancedMDLora", kLora, dimofi(kLora)},
    {"Libertinus Serif", "EnhancedMDLibertinus", kLibertinus, dimofi(kLibertinus)},
    {"New Computer Modern", "EnhancedMDNewCM10", kNewCM10, dimofi(kNewCM10)},
    {"New Computer Modern Book", "EnhancedMDNewCM10Book", kNewCM10Book, dimofi(kNewCM10Book)},
    {"New Computer Modern Sans", "EnhancedMDNewCMSans10", kNewCMSans10, dimofi(kNewCMSans10)},
    {"New Computer Modern Sans Book", "EnhancedMDNewCMSans10Book", kNewCMSans10Book, dimofi(kNewCMSans10Book)},
    {"New Computer Modern Mono", "EnhancedMDNewCMMono10", kNewCMMono10, dimofi(kNewCMMono10)},
    {"New Computer Modern Mono Book", "EnhancedMDNewCMMono10Book", kNewCMMono10Book, dimofi(kNewCMMono10Book)},
    {"New Computer Modern Uncial", "EnhancedMDNewCMUncial10", kNewCMUncial10, dimofi(kNewCMUncial10)},
};

static const MarkdownFontFamily kMarkdownMathFonts[] = {
    {"", "", nullptr, 0},
    {"Libertinus Math", "EnhancedMDLibertinusMath", kLibertinusMath, dimofi(kLibertinusMath)},
    {"New Computer Modern Math", "EnhancedMDNewCMMath", kNewCMMath, dimofi(kNewCMMath)},
    {"New Computer Modern Math Book", "EnhancedMDNewCMMathBook", kNewCMMathBook, dimofi(kNewCMMathBook)},
    {"New Computer Modern Sans Math", "EnhancedMDNewCMSansMath", kNewCMSansMath, dimofi(kNewCMSansMath)},
};

constexpr const char* kMarkdownFontRevision = "3d7870d8a8cdcd891e352e08f5c4ecc3451680f0";
constexpr const char* kMarkdownFontDownloadRoot =
    "https://raw.githubusercontent.com/abelokoj/sumatrapdf-enhanced/3d7870d8a8cdcd891e352e08f5c4ecc3451680f0/src/fonts/"
    "markdown/";

struct MarkdownFontAsset {
    const char* file;
    const char* path;
    int bytes;
    const char* sha256;
};
static const MarkdownFontAsset kMarkdownFontAssets[] = {
    {"Lora-Regular.otf", "lora/otf/Lora-Regular.otf", 100512,
     "ceb776d46e73fbb6d6ca6af6d3130a5d104f0cf2cdcf785bea7ff4e1dec3d024"},
    {"Lora-Italic.otf", "lora/otf/Lora-Italic.otf", 107844,
     "1625d70254d72122f831a71774f7a7c5957b88c64ead90b8b00986d6d5ae4aa8"},
    {"Lora-Medium.otf", "lora/otf/Lora-Medium.otf", 110924,
     "12d17900f9ad5645b78263f87bc9a3fe43d8bc217ad5e40e0f50ec5c5e286237"},
    {"Lora-MediumItalic.otf", "lora/otf/Lora-MediumItalic.otf", 121820,
     "4989466bbe7c1e9279afb63e478bf7ceef8008298b43f99b41b6ab1e899192c9"},
    {"Lora-SemiBold.otf", "lora/otf/Lora-SemiBold.otf", 110880,
     "fb133296ae7a606e1111721b7d606ebc9eb24892832a43ec5dfde975a94ddfaa"},
    {"Lora-SemiBoldItalic.otf", "lora/otf/Lora-SemiBoldItalic.otf", 121388,
     "4d50d35b4bac692d1cc8e70d6060ee03cd4303054d065ddc8b7536ca29d9e4a9"},
    {"Lora-Bold.otf", "lora/otf/Lora-Bold.otf", 103096,
     "612e89ea9b36d4fc3331e649adda76fb554b6ac44c721692f1c9adef692c89db"},
    {"Lora-BoldItalic.otf", "lora/otf/Lora-BoldItalic.otf", 106536,
     "dcd3e89a9c5b9f1972f54ba507b84072df3ac9caacd0e2c866741c1eca694d3a"},
    {"LibertinusSerif-Regular.otf", "libertinus/LibertinusSerif-Regular.otf", 337132,
     "fcf06307a77367394fcb0ccb241e59eea70dba3d732be309647611224679c733"},
    {"LibertinusSerif-Italic.otf", "libertinus/LibertinusSerif-Italic.otf", 332312,
     "9a393d63d6e05f620d3dc0190dfd35a8ede58c0808cf0fc9de7fcb9c723e4c24"},
    {"LibertinusSerif-Semibold.otf", "libertinus/LibertinusSerif-Semibold.otf", 291640,
     "a4b3f28e85881db34695c1f005e4c79233a6caf3a2bd286c9b418c025fb99308"},
    {"LibertinusSerif-SemiboldItalic.otf", "libertinus/LibertinusSerif-SemiboldItalic.otf", 343148,
     "397f0d7aba35ae6a988948ae046c14c6b1e0d270fcc6e292b8bc29fd625c6101"},
    {"LibertinusSerif-Bold.otf", "libertinus/LibertinusSerif-Bold.otf", 293908,
     "0264914210ed51b3231ebc92ce529e9f2e166ba9eebf0cd4a579558690a27b64"},
    {"LibertinusSerif-BoldItalic.otf", "libertinus/LibertinusSerif-BoldItalic.otf", 259176,
     "47a665259f09f554f5d133d7718cdad43ff462c6a6b2328f38023465e62d57ce"},
    {"EnhancedWebNewCM10-Regular.woff2", "browser/EnhancedWebNewCM10-Regular.woff2", 285008,
     "500f81394d8c106f143ab23ae18125f08447be1332f6c959ec56f46519cd0afc"},
    {"EnhancedWebNewCM10-Italic.woff2", "browser/EnhancedWebNewCM10-Italic.woff2", 295056,
     "9c51a534b56c5a78bf4d1aae3669e5d3c98110c2e903fe8bc0aa90121d911fda"},
    {"EnhancedWebNewCM10-Bold.woff2", "browser/EnhancedWebNewCM10-Bold.woff2", 243604,
     "e70507ce7059249d8ff770bc4a0e0134b90e19f5d35bac20664087e24f427f0a"},
    {"EnhancedWebNewCM10-BoldItalic.woff2", "browser/EnhancedWebNewCM10-BoldItalic.woff2", 266124,
     "887e0ada0299c82dc45ec0ed1c652a72e9365b9863c00129fc1ec04889640c55"},
    {"EnhancedWebNewCM10-Book.woff2", "browser/EnhancedWebNewCM10-Book.woff2", 285064,
     "b489da918f0affe8b70a2b8926b8b583b118416568349831e399463874d45441"},
    {"EnhancedWebNewCM10-BookItalic.woff2", "browser/EnhancedWebNewCM10-BookItalic.woff2", 317964,
     "31cd727a9b230233e6805db39c5141d1aa4ab46c6b9a0a9ef484e086daf6a10f"},
    {"EnhancedWebNewCMSans10-Regular.woff2", "browser/EnhancedWebNewCMSans10-Regular.woff2", 256496,
     "d28825b83e487dc82aeb95b0d3ccee9c294423b6c40bdb41c5ba643b05f4aa18"},
    {"EnhancedWebNewCMSans10-Oblique.woff2", "browser/EnhancedWebNewCMSans10-Oblique.woff2", 238616,
     "5bc24bd1854479abe88c58feee6eb03c5f19332d7db358b02db620a472281de5"},
    {"EnhancedWebNewCMSans10-Bold.woff2", "browser/EnhancedWebNewCMSans10-Bold.woff2", 265748,
     "c7c11ba5446245062746db0bb2bf4aaaa660315baf110152c6e65eb0545c83de"},
    {"EnhancedWebNewCMSans10-BoldOblique.woff2", "browser/EnhancedWebNewCMSans10-BoldOblique.woff2", 237276,
     "f6bc2c4d78d4771ff7e32aca55aeed11e39a5af99f7eac15df5206c2f3157ade"},
    {"EnhancedWebNewCMSans10-Book.woff2", "browser/EnhancedWebNewCMSans10-Book.woff2", 235628,
     "6e6065f56548c503d21a7830ac39fcab0317c1d0f83ad5064226bfff6443d5a9"},
    {"EnhancedWebNewCMSans10-BookOblique.woff2", "browser/EnhancedWebNewCMSans10-BookOblique.woff2", 237796,
     "b4e4b6f3819f113215f6e3f5f3bcd05cf835225f2f656f82d2e07ae05befb016"},
    {"EnhancedWebNewCMMono10-Regular.woff2", "browser/EnhancedWebNewCMMono10-Regular.woff2", 187424,
     "b31dfd5e86246534f8da6fd3ca7e74ece86c20848278df3b3bbf790a2757a1d9"},
    {"EnhancedWebNewCMMono10-Italic.woff2", "browser/EnhancedWebNewCMMono10-Italic.woff2", 226552,
     "15dec8778ca32d625665c5e5c67a6476406d33986ab8c3c63f04992a8127a298"},
    {"EnhancedWebNewCMMono10-Bold.woff2", "browser/EnhancedWebNewCMMono10-Bold.woff2", 215068,
     "883d50a5dc353c4c0f2be4ed697fce44e13230b6f2915ec821ca1836ffa72aee"},
    {"EnhancedWebNewCMMono10-BoldOblique.woff2", "browser/EnhancedWebNewCMMono10-BoldOblique.woff2", 306452,
     "f574c7e82b82548d909d9dd1785d3f0444e2b82b4ec0fcfeb999ad4b129962dc"},
    {"EnhancedWebNewCMMono10-Book.woff2", "browser/EnhancedWebNewCMMono10-Book.woff2", 212036,
     "69e70e332fe00bb62f3097d7d5ff2a30d8790c42bb78964b3df261be75871f5e"},
    {"EnhancedWebNewCMMono10-BookItalic.woff2", "browser/EnhancedWebNewCMMono10-BookItalic.woff2", 259604,
     "fa9d2bee5d2caf281edd27f2afe80660764e5048d7a59c604d874b08872707e8"},
    {"EnhancedWebNewCMUncial10-Regular.woff2", "browser/EnhancedWebNewCMUncial10-Regular.woff2", 16632,
     "3e27b9e941bdca87aba1479bbf654e03a41362342777929fd599439a4fc3b01f"},
    {"EnhancedWebNewCMUncial10-Bold.woff2", "browser/EnhancedWebNewCMUncial10-Bold.woff2", 12344,
     "67c4521851881f81690045e8efbcc3db68dde71c2bde54e8fe9b6fbdfcb1a9cb"},
    {"LibertinusMath-Regular.otf", "libertinus/LibertinusMath-Regular.otf", 588848,
     "ded714b0d3808af527438ed7a85f16933133f66a12104c4a83db4cc248888011"},
    {"EnhancedWebNewCMMath-Regular.woff2", "browser/EnhancedWebNewCMMath-Regular.woff2", 573308,
     "f0c5053c0d5b006dcfe91b4762339c58774893d2be4d5d2898448d19a63a2759"},
    {"EnhancedWebNewCMMath-Bold.woff2", "browser/EnhancedWebNewCMMath-Bold.woff2", 476264,
     "b0b95987663d55f03a005e03bbe8d59a6d0a4a11910b88afdcd07edd16d05bdf"},
    {"EnhancedWebNewCMMath-Book.woff2", "browser/EnhancedWebNewCMMath-Book.woff2", 643564,
     "9056b3a789f443faf719b52d55a7a85d3575953b54cbffce1222bff4de7e86bc"},
    {"EnhancedWebNewCMSansMath-Regular.woff2", "browser/EnhancedWebNewCMSansMath-Regular.woff2", 542808,
     "03da9dfe4d749e206cebe974f02785b982b9ead0b632bddc1ff0e75b57cdc1dd"},
    {"lora/OFL.txt", "lora/OFL.txt", 4423, "1d9a970809ac804b582a6ce7f0ebc4e7fefcbfd7ff6299cad35ee656a21be716"},
    {"libertinus/OFL.txt", "libertinus/OFL.txt", 4426,
     "9aeecc8107c489ec1ec0068b0313e531a760edf3493705b32ab8ab8215a8794e"},
    {"new-computer-modern/License.txt", "new-computer-modern/License.txt", 33266,
     "8641346ee391ff97eb66c70f3d5d695792cf9c5b3c5d748c6158d49cf79646e3"},
    {"new-computer-modern/GUST-FONT-LICENSE.txt", "new-computer-modern/GUST-FONT-LICENSE.txt", 1417,
     "5eb61bb836bb1845ef668717cb15b382e997748ce2629e4388cc5e4c3fa4e433"},
    {"NewCM10-Bold.otf", "new-computer-modern/NewCM10-Bold.otf", 629288,
     "40b0b32b63655fe802679ee4a14adb29c762a4712095d9c84cf7df7852b5152e"},
    {"NewCM10-BoldItalic.otf", "new-computer-modern/NewCM10-BoldItalic.otf", 609000,
     "fb53f43500c8ffbcc6dfdea5a519961d475543174ea2a0dcbe16c08510299fdf"},
    {"NewCM10-Book.otf", "new-computer-modern/NewCM10-Book.otf", 694392,
     "005f9b4e13b2a133df36d4ebfbb8f5fe402854bcb5882deb500b9c0f72b1bb49"},
    {"NewCM10-BookItalic.otf", "new-computer-modern/NewCM10-BookItalic.otf", 699696,
     "d01f6c7b5bcdee2545823f1cf0a35698b9ccff1891a2f79a8e9938e5b8ea561e"},
    {"NewCM10-Italic.otf", "new-computer-modern/NewCM10-Italic.otf", 721176,
     "a55d07607eec48889b6e7e7d51d7703c3beb915a39690f34fe01b4e2a744a9d6"},
    {"NewCM10-Regular.otf", "new-computer-modern/NewCM10-Regular.otf", 717588,
     "328698d764ccdf7acf6bc1088aefd83a237f6a1d8b812de1e77ac5e4483bf3d1"},
    {"NewCMMath-Bold.otf", "new-computer-modern/NewCMMath-Bold.otf", 1232148,
     "c6c0e060da57d4f44274705afb956013047231dc62be5a4b02a351ab0dc43f2f"},
    {"NewCMMath-Book.otf", "new-computer-modern/NewCMMath-Book.otf", 1432068,
     "2ea09ebc9167b1e1a66f31390dc917f2d4004ecfca72d51b28010e4ad6becd95"},
    {"NewCMMath-Regular.otf", "new-computer-modern/NewCMMath-Regular.otf", 1306268,
     "d66ac1cc91c55c24d3636ae2df1238076debdff51841f9893fc5419cc2df3df7"},
    {"NewCMMono10-Bold.otf", "new-computer-modern/NewCMMono10-Bold.otf", 554392,
     "f651c3856b8dd5b8483b855e583e8e831f078ae60d39861c6142ecc9df1f34c4"},
    {"NewCMMono10-BoldOblique.otf", "new-computer-modern/NewCMMono10-BoldOblique.otf", 773356,
     "f8a7fed3ac5b7a9840ef003960c18f6d05cd57b76cf72536c93baea5162b51a0"},
    {"NewCMMono10-Book.otf", "new-computer-modern/NewCMMono10-Book.otf", 529816,
     "d5f2398e5198ea4112270fc6700130170c42c093c5c6af6fc19fe905def8953c"},
    {"NewCMMono10-BookItalic.otf", "new-computer-modern/NewCMMono10-BookItalic.otf", 603736,
     "f4b7a13365af080896fd09142866ab24d19ce6d3b72aeaf17cdee86beb4cbd8f"},
    {"NewCMMono10-Italic.otf", "new-computer-modern/NewCMMono10-Italic.otf", 547920,
     "974de80c47d467cc9e281d7d540321aee0dafb6fc25a03ad6b5831624e7f3d5e"},
    {"NewCMMono10-Regular.otf", "new-computer-modern/NewCMMono10-Regular.otf", 473580,
     "1a1582279205c4307407a2da27b646a6ef9381fe35aeecfe783a1bb8e13b5977"},
    {"NewCMSans10-Bold.otf", "new-computer-modern/NewCMSans10-Bold.otf", 670212,
     "e92f716dc6db995a407319520a01225afa3cfba94d36fe2d45ed6ecf6554ab6b"},
    {"NewCMSans10-BoldOblique.otf", "new-computer-modern/NewCMSans10-BoldOblique.otf", 569884,
     "618cc0452bbc3d8ea0d7d2a1031cd09f178d216bf9ad675f4daf8659ea1ef26c"},
    {"NewCMSans10-Book.otf", "new-computer-modern/NewCMSans10-Book.otf", 578180,
     "0ed20417390dff60e48568ea81851a119e96b6cc86564fe0f1de9ce73002f296"},
    {"NewCMSans10-BookOblique.otf", "new-computer-modern/NewCMSans10-BookOblique.otf", 554916,
     "378f5065ac20e0b64655a4cc50dd0fa9547a30f374c84a6361218d82a069f874"},
    {"NewCMSans10-Oblique.otf", "new-computer-modern/NewCMSans10-Oblique.otf", 559652,
     "93a03fce2778d459a14747d576803e82d12f653001a2292ef42aac0cb8e20fc7"},
    {"NewCMSans10-Regular.otf", "new-computer-modern/NewCMSans10-Regular.otf", 648436,
     "ca267604ebd4b51562f263df4206a2ce25f71fb46701a438e145139295f567f0"},
    {"NewCMSansMath-Regular.otf", "new-computer-modern/NewCMSansMath-Regular.otf", 1183540,
     "2976236474e4e92ea68d9140c5b05fa1b98818a0a4a30cf8881c4c45ca951fd3"},
    {"NewCMUncial10-Bold.otf", "new-computer-modern/NewCMUncial10-Bold.otf", 21220,
     "3d6c2196a8e7e5b4450588974e8119a9a32d2503327a4695cc9eb7ed3a15deac"},
    {"NewCMUncial10-Regular.otf", "new-computer-modern/NewCMUncial10-Regular.otf", 30368,
     "9f03ed41d0e2d9b435c981a652142488fb325963c4efc1b13ad752d2cb9a8147"},
};
static Mutex gMarkdownFontInstallLock;
static Mutex gMarkdownFontVerifyLock;
static StrVec gVerifiedFontPaths;
static Vec<FILETIME> gVerifiedFontTimes;

static const MarkdownFontAsset* FontAsset(Str name) {
    for (const auto& asset : kMarkdownFontAssets) {
        if (str::Eq(name, Str(asset.file))) return &asset;
    }
    return nullptr;
}
TempStr MarkdownFontsCacheDir() {
    return path::ToOSTemp(path::JoinTemp(GetAppDataDirTemp(), StrL("markdown-fonts"), Str(kMarkdownFontRevision)));
}
static TempStr FontCachePath(const MarkdownFontAsset* asset, Str cacheRoot = {}) {
    if (!len(cacheRoot)) cacheRoot = MarkdownFontsCacheDir();
    // SHCreateDirectoryEx requires Windows separators, including nested asset paths.
    return path::ToOSTemp(path::JoinTemp(cacheRoot, Str(asset->path)));
}
static bool ValidFontAsset(Str bytes, const MarkdownFontAsset* asset) {
    if (len(bytes) != asset->bytes) return false;
    u8 hash[32]{};
    CalcSHA2Digest(bytes, hash);
    return str::Eq(str::MemToHexTemp(Str((char*)hash, sizeof(hash))), Str(asset->sha256));
}
// Check each cache file once, then invalidate the check when its timestamp changes.
static bool CachedFontAsset(const MarkdownFontAsset* asset, Str cacheRoot) {
    if (!HasPermission(Perm::DiskAccess)) return false;
    TempStr path = FontCachePath(asset, cacheRoot);
    if (file::GetSize(path) != asset->bytes) return false;
    FILETIME modified = file::GetModificationTime(path);
    AutoUnlockMutex lock(&gMarkdownFontVerifyLock);
    int index = gVerifiedFontPaths.Find(path);
    if (index >= 0 && CompareFileTime(&modified, &gVerifiedFontTimes[index]) == 0) return true;
    Str data = file::ReadFile(path);
    bool valid = ValidFontAsset(data, asset);
    str::Free(data);
    if (!valid) return false;
    if (index < 0) {
        gVerifiedFontPaths.Append(path);
        VecAppend(gVerifiedFontTimes, modified);
    } else {
        gVerifiedFontTimes[index] = modified;
    }
    return true;
}
static bool FontDownloadError(Str* error, Str message) {
    if (error) str::ReplaceWithCopy(error, message);
    return false;
}
static bool DownloadFontAsset(const MarkdownFontAsset* asset, Str* error, Str cacheRoot) {
    if (CachedFontAsset(asset, cacheRoot)) return true;
    TempStr path = FontCachePath(asset, cacheRoot);
    int directoryError = 0;
    if (!dir::CreateAll(path::GetDirTemp(path), &directoryError)) {
        logf("DownloadMarkdownFonts: cannot create '%s', Windows error %d\n", path::GetDirTemp(path), directoryError);
        return FontDownloadError(error,
                                 fmt("Cannot create the font download folder (Windows error %d).", directoryError));
    }
    // A process-specific staging name keeps separate app instances independent.
    TempStr pending = fmt("%s.%d.pending", path, (int)GetCurrentProcessId());
    TempStr url = str::JoinTemp(Str(kMarkdownFontDownloadRoot), Str(asset->path));
    bool ok = HttpGetToFile(url, pending, {}, asset->bytes);
    if (ok) {
        Str data = file::ReadFile(pending);
        ok = ValidFontAsset(data, asset);
        str::Free(data);
    }
    if (ok)
        ok = MoveFileExW(CWStrTemp(pending), CWStrTemp(path), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    file::Delete(pending);
    if (!ok)
        return FontDownloadError(error, StrL("The font download failed or its checksum did not match. Try again."));
    return true;
}

int MarkdownFontCount(MarkdownFontKind kind) {
    return kind == MarkdownFontKind::Text ? dimofi(kMarkdownTextFonts) : dimofi(kMarkdownMathFonts);
}
static const MarkdownFontFamily* FontFamily(MarkdownFontKind kind, int index) {
    if (index < 0 || index >= MarkdownFontCount(kind)) return nullptr;
    return kind == MarkdownFontKind::Text ? &kMarkdownTextFonts[index] : &kMarkdownMathFonts[index];
}
Str MarkdownFontName(MarkdownFontKind kind, int index) {
    auto* font = FontFamily(kind, index);
    return font ? Str(font->name) : Str{};
}
int MarkdownFontIndex(MarkdownFontKind kind, Str name) {
    if (!len(name)) return 0;
    for (int i = 1; i < MarkdownFontCount(kind); i++) {
        if (str::EqI(name, MarkdownFontName(kind, i))) return i;
    }
    return 0;
}
void MarkdownFontChoices(MarkdownFontKind kind, StrVec& names) {
    names.Append(kind == MarkdownFontKind::Text ? Tr("Follow interface font") : Tr("Match text font"));
    for (int i = 1; i < MarkdownFontCount(kind); i++) names.Append(MarkdownFontName(kind, i));
}
static bool SystemFontName(Str name) {
    WStr wide = ToWStrTemp(name);
    if (!len(wide) || len(wide) >= LF_FACESIZE || wide.s[0] == L'@' || str::StartsWith(name, StrL("EDict")))
        return false;
    for (int i = 0; i < len(name); i++) {
        u8 c = (u8)name.s[i];
        if (c < 32 || c == 127) return false;
    }
    return true;
}

static int CALLBACK MarkdownFontEnum(const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM data) {
    auto* names = (StrVec*)data;
    Str name = ToUtf8Temp(font->lfFaceName);
    if (!SystemFontName(name)) return 1;
    for (Str existing : *names) {
        if (str::EqI(existing, name)) return 1;
    }
    names->Append(name);
    return 1;
}

void InstalledMarkdownFonts(StrVec& names) {
    HDC dc = GetDC(nullptr);
    if (!dc) return;
    LOGFONTW font{};
    font.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &font, MarkdownFontEnum, (LPARAM)&names, 0);
    ReleaseDC(nullptr, dc);
    SortNoCase(&names);
}

struct MarkdownSystemFontMatch {
    Str name;
    bool found = false;
};

static int CALLBACK MatchMarkdownFont(const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM data) {
    auto* match = (MarkdownSystemFontMatch*)data;
    match->found = str::EqI(ToUtf8Temp(font->lfFaceName), match->name);
    return match->found ? 0 : 1;
}

bool IsMarkdownSystemFont(Str name) {
    if (!SystemFontName(name)) return false;
    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    LOGFONTW font{};
    font.lfCharSet = DEFAULT_CHARSET;
    wstr::BufSet(WStr(font.lfFaceName, dimof(font.lfFaceName)), ToWStrTemp(name));
    MarkdownSystemFontMatch match{name};
    EnumFontFamiliesExW(dc, &font, MatchMarkdownFont, (LPARAM)&match, 0);
    ReleaseDC(nullptr, dc);
    return match.found;
}

static Str FontResource(const WCHAR* name) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, name, RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
    const char* data = loaded ? (const char*)LockResource(loaded) : nullptr;
    return data ? Str(data, (int)SizeofResource(module, resource)) : Str{};
}
Str MarkdownSuppliedCss(bool dark) {
    return FontResource(dark ? L"ENHANCED_MARKDOWN_DARK_CSS" : L"ENHANCED_MARKDOWN_LIGHT_CSS");
}
// Returns owned bytes; resource requests never download fonts.
Str MarkdownFontResource(Str url) {
    if (!HasPermission(Perm::DiskAccess)) return {};
    if (!str::TrimPrefix(url, Str(kMarkdownFontHost))) return {};
    const auto* asset = FontAsset(url);
    if (!asset || (!str::EndsWith(url, StrL(".otf")) && !str::EndsWith(url, StrL(".woff2")))) return {};
    // A browser resource request never starts a download.
    TempStr path = FontCachePath(asset);
    if (file::GetSize(path) != asset->bytes) return {};
    Str data = file::ReadFile(path);
    if (ValidFontAsset(data, asset)) return data;
    str::Free(data);
    return {};
}
int MarkdownMathFontIndex(Str text, Str math) {
    int mathIndex = MarkdownFontIndex(MarkdownFontKind::Math, math);
    if (!mathIndex && IsMarkdownSystemFont(math)) return 0;
    int textIndex = MarkdownFontIndex(MarkdownFontKind::Text, text);
    if (mathIndex || !textIndex) return mathIndex;
    if (textIndex == 4) return 3;
    if (textIndex == 5 || textIndex == 6) return 4;
    return textIndex >= 3 ? 2 : 1;
}
static bool ValidFontChoice(MarkdownFontKind kind, Str name) {
    return !len(name) || MarkdownFontIndex(kind, name) != 0 || IsMarkdownSystemFont(name);
}
static void FontChoiceAssets(Str text, Str math, Vec<const MarkdownFontAsset*>& assets) {
    const MarkdownFontFamily* fonts[] = {
        FontFamily(MarkdownFontKind::Text, MarkdownFontIndex(MarkdownFontKind::Text, text)),
        FontFamily(MarkdownFontKind::Math, MarkdownMathFontIndex(text, math))};
    bool lora = false, libertinus = false, newCM = false;
    for (const auto* family : fonts) {
        for (int j = 0; j < family->count; j++) {
            const auto* asset = FontAsset(Str(family->faces[j].file));
            lora |= str::StartsWith(Str(asset->path), StrL("lora/"));
            libertinus |= str::StartsWith(Str(asset->path), StrL("libertinus/"));
            newCM |= str::StartsWith(Str(asset->path), StrL("browser/"));
            if (!VecContains(assets, asset)) VecAppend(assets, asset);
        }
    }
    if (lora) VecAppend(assets, FontAsset(StrL("lora/OFL.txt")));
    if (libertinus) VecAppend(assets, FontAsset(StrL("libertinus/OFL.txt")));
    if (newCM) {
        VecAppend(assets, FontAsset(StrL("new-computer-modern/License.txt")));
        VecAppend(assets, FontAsset(StrL("new-computer-modern/GUST-FONT-LICENSE.txt")));
    }
}
static bool CachedFontAssets(const Vec<const MarkdownFontAsset*>& assets, Str cacheRoot) {
    for (const auto* asset : assets) {
        if (!CachedFontAsset(asset, cacheRoot)) return false;
    }
    return true;
}
bool MarkdownFontsAvailable(Str text, Str math) {
    if (!ValidFontChoice(MarkdownFontKind::Text, text) || !ValidFontChoice(MarkdownFontKind::Math, math)) return false;
    Vec<const MarkdownFontAsset*> assets;
    FontChoiceAssets(text, math, assets);
    if (!len(assets)) return true;
    if (!HasPermission(Perm::DiskAccess)) return false;
    return CachedFontAssets(assets, MarkdownFontsCacheDir());
}
bool DownloadMarkdownFonts(Str text, Str math, Str* error, Str cacheRoot) {
    if (error) str::ReplaceWithCopy(error, {});
    if (!ValidFontChoice(MarkdownFontKind::Text, text) || !ValidFontChoice(MarkdownFontKind::Math, math))
        return FontDownloadError(error, StrL("Unknown Markdown font."));
    Vec<const MarkdownFontAsset*> assets;
    FontChoiceAssets(text, math, assets);
    if (!len(assets)) return true;
    if (!HasPermission(Perm::DiskAccess))
        return FontDownloadError(error, StrL("Downloading fonts is disabled in restricted mode."));
    // Keep one destination even if the user changes the data folder during a download.
    if (!len(cacheRoot)) cacheRoot = MarkdownFontsCacheDir();
    if (CachedFontAssets(assets, cacheRoot)) return true;
    if (!HasPermission(Perm::InternetAccess))
        return FontDownloadError(error, StrL("Downloading fonts is disabled in restricted mode."));
    AutoUnlockMutex lock(&gMarkdownFontInstallLock);
    for (const auto* asset : assets) {
        if (!DownloadFontAsset(asset, error, cacheRoot)) return false;
    }
    TempStr notice = path::ToOSTemp(path::JoinTemp(cacheRoot, StrL("FONT-SOURCES.txt")));
    if (!file::WriteFile(notice, StrL("Optional Markdown fonts downloaded from SumatraPDF Enhanced.\r\n"
                                      "Lora 3.021 and Libertinus 7.051: SIL Open Font License 1.1.\r\n"
                                      "New Computer Modern 8.1.1: see the accompanying license notices.\r\n"
                                      "Fonts, upstream releases, notices and NewCM source archive:\r\n"
                                      "https://github.com/abelokoj/sumatrapdf-enhanced/tree/"
                                      "3d7870d8a8cdcd891e352e08f5c4ecc3451680f0/src/fonts/markdown\r\n")))
        return FontDownloadError(error, StrL("Cannot save the font license notice."));
    return true;
}
struct ReadingFontFamily {
    const char* name;
    const char* face;
    const char* files[4];
};
static const ReadingFontFamily kReadingFonts[] = {
    {"Lora", "Lora", {"Lora-Regular.otf", "Lora-Italic.otf", "Lora-Bold.otf", "Lora-BoldItalic.otf"}},
    {"Libertinus Serif",
     "Libertinus Serif",
     {"LibertinusSerif-Regular.otf", "LibertinusSerif-Italic.otf", "LibertinusSerif-Bold.otf",
      "LibertinusSerif-BoldItalic.otf"}},
    {"New Computer Modern",
     "NewComputerModern10",
     {"NewCM10-Regular.otf", "NewCM10-Italic.otf", "NewCM10-Bold.otf", "NewCM10-BoldItalic.otf"}},
    {"New Computer Modern Book",
     "EDictCMBook",
     {"NewCM10-Book.otf", "NewCM10-BookItalic.otf", "NewCM10-Bold.otf", "NewCM10-BoldItalic.otf"}},
    {"New Computer Modern Sans",
     "NewComputerModernSans10",
     {"NewCMSans10-Regular.otf", "NewCMSans10-Oblique.otf", "NewCMSans10-Bold.otf", "NewCMSans10-BoldOblique.otf"}},
    {"New Computer Modern Sans Book",
     "EDictCMSansBook",
     {"NewCMSans10-Book.otf", "NewCMSans10-BookOblique.otf", "NewCMSans10-Bold.otf", "NewCMSans10-BoldOblique.otf"}},
    {"New Computer Modern Mono",
     "NewComputerModernMono10",
     {"NewCMMono10-Regular.otf", "NewCMMono10-Italic.otf", "NewCMMono10-Bold.otf", "NewCMMono10-BoldOblique.otf"}},
    {"New Computer Modern Mono Book",
     "EDictCMMonoBook",
     {"NewCMMono10-Book.otf", "NewCMMono10-BookItalic.otf", "NewCMMono10-Bold.otf", "NewCMMono10-BoldOblique.otf"}},
    {"New Computer Modern Uncial",
     "NewComputerModernUncial10",
     {"NewCMUncial10-Regular.otf", "NewCMUncial10-Bold.otf"}},
    {"Libertinus Math", "Libertinus Math", {"LibertinusMath-Regular.otf"}},
    {"New Computer Modern Math", "NewComputerModernMath", {"NewCMMath-Regular.otf", "NewCMMath-Bold.otf"}},
    {"New Computer Modern Math Book", "EDictCMMathBook", {"NewCMMath-Book.otf", "NewCMMath-Bold.otf"}},
    {"New Computer Modern Sans Math", "NewComputerModernSansMath", {"NewCMSansMath-Regular.otf"}},
};
static const ReadingFontFamily* ReadingFamily(Str name) {
    for (const auto& font : kReadingFonts) {
        if (str::EqI(name, Str(font.name))) return &font;
    }
    return nullptr;
}
static void ReadingAssets(const ReadingFontFamily* font, Vec<const MarkdownFontAsset*>& assets) {
    for (auto* file : font->files) {
        if (file) VecAppend(assets, FontAsset(Str(file)));
    }
    Str path = Str(assets[0]->path);
    if (str::StartsWith(path, StrL("lora/")))
        VecAppend(assets, FontAsset(StrL("lora/OFL.txt")));
    else if (str::StartsWith(path, StrL("libertinus/")))
        VecAppend(assets, FontAsset(StrL("libertinus/OFL.txt")));
    else {
        VecAppend(assets, FontAsset(StrL("new-computer-modern/License.txt")));
        VecAppend(assets, FontAsset(StrL("new-computer-modern/GUST-FONT-LICENSE.txt")));
    }
}
bool ReadingFontAvailable(Str name) {
    const auto* font = ReadingFamily(name);
    if (!font) return true;
    Vec<const MarkdownFontAsset*> assets;
    ReadingAssets(font, assets);
    return CachedFontAssets(assets, MarkdownFontsCacheDir());
}
bool DownloadReadingFont(Str name, Str* error, Str cacheRoot) {
    const auto* font = ReadingFamily(name);
    if (!font) return true;
    if (!len(cacheRoot)) cacheRoot = MarkdownFontsCacheDir();
    Vec<const MarkdownFontAsset*> assets;
    ReadingAssets(font, assets);
    if (CachedFontAssets(assets, cacheRoot)) return true;
    if (!HasPermission(Perm::DiskAccess) || !HasPermission(Perm::InternetAccess))
        return FontDownloadError(error, StrL("Downloading fonts is disabled in restricted mode."));
    AutoUnlockMutex lock(&gMarkdownFontInstallLock);
    for (const auto* asset : assets) {
        if (!DownloadFontAsset(asset, error, cacheRoot)) return false;
    }
    return true;
}
static void FontPut16(u8* p, u16 n) {
    p[0] = (u8)(n >> 8);
    p[1] = (u8)n;
}
static void FontPut32(u8* p, u32 n) {
    p[0] = (u8)(n >> 24);
    p[1] = (u8)(n >> 16);
    p[2] = (u8)(n >> 8);
    p[3] = (u8)n;
}
static u32 FontChecksum(const u8* p, u32 bytes) {
    u32 sum = 0;
    for (u32 i = 0; i < bytes; i += 4) {
        u32 word = 0;
        for (u32 j = 0; j < 4; j++) word = (word << 8) | (i + j < bytes ? p[i + j] : 0);
        sum += word;
    }
    return sum;
}
// Book and regular NewCM have identical GDI family names. Rename only the
// process-private name table; the verified download and glyph data stay intact.
static bool AliasReadingFont(Str bytes, Str alias) {
    auto* data = (u8*)bytes.s;
    u32 size = (u32)len(bytes);
    if (size < 12) return false;
    u32 count = UInt16BE(data + 4), nameOffset = 0, nameSize = 0;
    u8* nameEntry = nullptr;
    u8* head = nullptr;
    if (12 + count * 16 > size) return false;
    for (u32 i = 0; i < count; i++) {
        u8* entry = data + 12 + i * 16;
        u32 offset = UInt32BE(entry + 8), tableSize = UInt32BE(entry + 12);
        if (offset > size || tableSize > size - offset) return false;
        if (!memcmp(entry, "name", 4)) {
            nameEntry = entry;
            nameOffset = offset;
            nameSize = tableSize;
        }
        if (!memcmp(entry, "head", 4) && tableSize >= 12) head = data + offset;
    }
    if (!nameEntry || !head || nameSize < 6) return false;
    u8* name = data + nameOffset;
    u32 records = UInt16BE(name + 2), strings = UInt16BE(name + 4);
    if (6 + records * 12 > nameSize || strings > nameSize) return false;
    for (u32 i = 0; i < records; i++) {
        u8* record = name + 6 + i * 12;
        u16 id = UInt16BE(record + 6), platform = UInt16BE(record);
        if (id != 1 && id != 4 && id != 16) continue;
        if (platform > 3 || platform == 2) continue;
        u32 width = platform == 1 ? 1 : 2, length = UInt16BE(record + 8);
        u32 offset = strings + UInt16BE(record + 10), needed = (u32)len(alias) * width;
        if (needed > length || offset > nameSize || length > nameSize - offset) return false;
        for (int j = 0; j < len(alias); j++) {
            if (width == 2) name[offset + j * 2] = 0;
            name[offset + j * width + width - 1] = (u8)alias.s[j];
        }
        FontPut16(record + 8, (u16)needed);
    }
    FontPut32(nameEntry + 4, FontChecksum(name, nameSize));
    FontPut32(head + 8, 0);
    FontPut32(head + 8, 0xb1b0afba - FontChecksum(data, size));
    return true;
}

// Registration is private to this app; opening a dictionary never downloads a font.
TempStr ResolveReadingFont(Str name) {
    const auto* font = ReadingFamily(name);
    if (!font) return name;
    if (!ReadingFontAvailable(name)) return {};
    static StrVec registered;
    for (auto* file : font->files) {
        if (!file) continue;
        auto* asset = FontAsset(Str(file));
        TempStr path = FontCachePath(asset);
        TempStr key = fmt("%s|%s", path, Str(font->face));
        if (registered.Contains(key)) continue;
        Str bytes = file::ReadFile(path);
        defer {
            str::Free(bytes);
        };
        if (!ValidFontAsset(bytes, asset)) return {};
        if (str::StartsWith(Str(font->face), StrL("EDict")) && !AliasReadingFont(bytes, Str(font->face))) return {};
        DWORD faces = 0;
        if (!AddFontMemResourceEx(bytes.s, len(bytes), nullptr, &faces)) return {};
        registered.Append(key);
    }
    return Str(font->face);
}

static void AppendFontCss(str::Builder& css, const MarkdownFontFamily* font) {
    for (int i = 0; i < font->count; i++) {
        auto& face = font->faces[i];
        css.Append(
            fmt("@font-face{font-family:'%s';font-weight:%d;font-style:%s;font-display:swap;"
                "src:url('%s%s') format('%s');}\n",
                Str(font->cssName), face.weight, Str(face.style), Str(kMarkdownFontHost), Str(face.file),
                str::EndsWith(Str(face.file), StrL(".woff2")) ? StrL("woff2") : StrL("opentype")));
    }
}
static TempStr QuotedFontCss(Str name) {
    str::Builder css;
    css.AppendChar('\'');
    for (int i = 0; i < len(name); i++) {
        u8 c = (u8)name.s[i];
        if (c < 32 || c == 127 || c == '\'' || c == '\\' || c == '<' || c == '>' || c == '&')
            css.Append(fmt("\\%x ", c));
        else
            css.AppendChar((char)c);
    }
    css.AppendChar('\'');
    return ToStrTemp(css);
}

static TempStr BuildMarkdownFontCss(Str fontName, Str mathFontName) {
    int textIndex = MarkdownFontIndex(MarkdownFontKind::Text, fontName);
    auto* font = FontFamily(MarkdownFontKind::Text, textIndex);
    int mathIndex = MarkdownMathFontIndex(fontName, mathFontName);
    auto* math = FontFamily(MarkdownFontKind::Math, mathIndex);
    str::Builder css;
    AppendFontCss(css, font);
    AppendFontCss(css, math);
    bool systemText = !textIndex && IsMarkdownSystemFont(fontName);
    bool systemMath = !mathIndex && IsMarkdownSystemFont(mathFontName);
    Str family = font->count  ? (math->count ? fmt("'%s','%s',serif", Str(font->cssName), Str(math->cssName))
                                             : fmt("'%s',serif", Str(font->cssName)))
                 : systemText ? fmt("%s,serif", QuotedFontCss(fontName))
                              : StrL("var(--enhanced-ui-font-family)");
    Str mathFamily = math->count                        ? fmt("'%s',math", Str(math->cssName))
                     : systemMath                       ? fmt("%s,math", QuotedFontCss(mathFontName))
                     : systemText && !len(mathFontName) ? fmt("%s,math", QuotedFontCss(fontName))
                                                        : StrL("math");
    css.Append(
        fmt(":root{--enhanced-markdown-font-family:%s;--enhanced-markdown-math-font-family:%s;"
            "--font-serif:var(--enhanced-markdown-font-family);"
            "--base-font-size:var(--enhanced-ui-font-size)!important;}\n"
            "math{font-family:var(--enhanced-markdown-math-font-family);}",
            family, mathFamily));
    return ToStrTemp(css);
}
TempStr MarkdownFontCss(Str fontName, Str mathFontName) {
    if (!MarkdownFontsAvailable(fontName, mathFontName)) return BuildMarkdownFontCss({}, {});
    return BuildMarkdownFontCss(fontName, mathFontName);
}
#if IS_DEBUG
static void NativeReadingFontTests() {
    WCHAR fixtures[1024]{};
    DWORD length = GetEnvironmentVariableW(L"SUMATRA_READING_FONT_FIXTURES", fixtures, dimof(fixtures));
    if (!length || length >= dimof(fixtures)) return;
    Str original = str::Dup(GetAppDataDirTemp());
    Str profile = str::Dup(GetTempFilePathTemp(StrL("rdf")));
    file::Delete(profile);
    SetAppDataDir(profile);
    defer {
        SetAppDataDir(original);
        str::Free(original);
        str::Free(profile);
    };
    for (const auto& font : kReadingFonts) {
        Vec<const MarkdownFontAsset*> assets;
        ReadingAssets(&font, assets);
        for (const auto* asset : assets) {
            TempStr from = path::JoinTemp(ToUtf8Temp(fixtures), Str(asset->path));
            TempStr to = FontCachePath(asset);
            utassert(dir::CreateAll(path::GetDirTemp(to)));
            if (!file::Exists(to)) utassert(CopyFileW(CWStrTemp(from), CWStrTemp(to), FALSE));
        }
        utassert(ReadingFontAvailable(Str(font.name)));
        Str face = ResolveReadingFont(Str(font.name));
        utassert(str::Eq(face, Str(font.face)));
        LOGFONTW lf{};
        lf.lfHeight = -24;
        lf.lfCharSet = DEFAULT_CHARSET;
        wstr::BufSet(WStr(lf.lfFaceName, dimof(lf.lfFaceName)), ToWStrTemp(face));
        HFONT hfont = CreateFontIndirectW(&lf);
        HDC dc = GetDC(nullptr);
        HGDIOBJ old = SelectObject(dc, hfont);
        WCHAR actual[LF_FACESIZE]{};
        GetTextFaceW(dc, dimof(actual), actual);
        utassert(str::EqI(ToUtf8Temp(actual), face));
        logf("Dictionary native font: %s => %s\n", Str(font.name), ToUtf8Temp(actual));
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
        DeleteObject(hfont);
    }
}
void MarkdownFonts_UnitTests() {
    NativeReadingFontTests();
    StrVec names;
    MarkdownFontChoices(MarkdownFontKind::Text, names);
    utassert(len(names) == MarkdownFontCount(MarkdownFontKind::Text));
    for (MarkdownFontKind kind : {MarkdownFontKind::Text, MarkdownFontKind::Math}) {
        for (int i = 1; i < MarkdownFontCount(kind); i++) {
            auto* font = FontFamily(kind, i);
            utassert(MarkdownFontIndex(kind, Str(font->name)) == i);
            for (int j = 0; j < font->count; j++) {
                auto& face = font->faces[j];
                const auto* asset = FontAsset(Str(face.file));
                utassert(asset && asset->bytes > 1000 && strlen(asset->sha256) == 64);
                utassert(!str::Contains(Str(asset->path), StrL("..")));
                utassert(!path::IsAbsolute(Str(asset->path)));
                utassert(str::Contains(FontCachePath(asset), Str(kMarkdownFontRevision)));
            }
        }
    }
    utassert(len(MarkdownFontResource(StrL("https://example.com/LibertinusSerif-Regular.otf"))) == 0);
    utassert(len(MarkdownFontResource(fmt("%s../Settings.h", Str(kMarkdownFontHost)))) == 0);
    utassert(MarkdownFontsAvailable({}, {}));
    utassert(!MarkdownFontsAvailable(StrL("Not a font"), {}));
    StrVec systemFonts;
    InstalledMarkdownFonts(systemFonts);
    utassert(len(systemFonts) > 0);
    for (Str name : systemFonts) {
        utassert(IsMarkdownSystemFont(name));
        utassert(name.s[0] != '@');
    }
    utassert(!IsMarkdownSystemFont(StrL("Not a font")));
    utassert(!IsMarkdownSystemFont(StrL("@Arial")));
    utassert(!IsMarkdownSystemFont(StrL("Arial\n")));
    Str systemFont = len(systemFonts) ? systemFonts[0] : StrL("Arial");
    utassert(MarkdownFontsAvailable(systemFont, {}));
    utassert(MarkdownFontsAvailable({}, systemFont));
    utassert(MarkdownFontsAvailable(systemFont, systemFont));
    utassert(MarkdownMathFontIndex(StrL("Lora"), systemFont) == 0);
    utassert(MarkdownMathFontIndex(systemFont, {}) == 0);
    TempStr systemCss = MarkdownFontCss(systemFont, {});
    utassert(str::Contains(systemCss, systemFont));
    utassert(!str::Contains(systemCss, StrL("@font-face")));
    utassert(str::Contains(MarkdownFontCss({}, systemFont), systemFont));
    Str systemError;
    utassert(DownloadMarkdownFonts(systemFont, systemFont, &systemError));
    utassert(len(systemError) == 0);
    str::Free(systemError);
    utassert(MarkdownMathFontIndex({}, {}) == 0);
    utassert(MarkdownMathFontIndex(StrL("Lora"), {}) == 1);
    utassert(MarkdownMathFontIndex(StrL("New Computer Modern Book"), {}) == 3);
    utassert(MarkdownMathFontIndex(StrL("New Computer Modern Sans"), {}) == 4);
    utassert(!str::Contains(MarkdownFontCss({}, {}), StrL("@font-face")));
    utassert(str::Eq(QuotedFontCss(StrL("a\'b\\c<d")), StrL("\'a\\27 b\\5c c\\3c d\'")));
    utassert(!str::Contains(MarkdownFontCss(StrL("Not a font"), {}), StrL("Not a font")));
    MarkdownFontAsset sample{"sample", "sample", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"};
    utassert(ValidFontAsset(StrL("abc"), &sample));
    utassert(!ValidFontAsset(StrL("abd"), &sample));
    utassert(!ValidFontAsset(StrL("abcd"), &sample));
    utassert(str::Eq(FontCachePath(FontAsset(StrL("Lora-Regular.otf")), StrL("C:/font-cache")),
                     StrL("C:\\font-cache\\lora\\otf\\Lora-Regular.otf")));
    utassert(FontAsset(StrL("../Lora-Regular.otf")) == nullptr);
    utassert(FontAsset(StrL("Lora-Regular.otf?x=1")) == nullptr);
    Str error;
    utassert(!DownloadMarkdownFonts(StrL("Not a font"), {}, &error));
    utassert(len(error) > 0);
    utassert(DownloadMarkdownFonts({}, {}, &error));
    utassert(len(error) == 0);
    str::Free(error);
    utassert(MarkdownFontIndex(MarkdownFontKind::Text, StrL("Unknown font")) == 0);
    TempStr css = BuildMarkdownFontCss(StrL("Libertinus Serif"), {});
    utassert(str::Contains(css, StrL("font-weight:600;font-style:italic")));
    utassert(str::Contains(css, StrL("EnhancedMDLibertinusMath")));
    utassert(str::Contains(css, StrL("'EnhancedMDLibertinus','EnhancedMDLibertinusMath',serif")));
    utassert(
        str::Contains(BuildMarkdownFontCss(StrL("New Computer Modern Book"), {}), StrL("EnhancedMDNewCMMathBook")));
    utassert(
        str::Contains(BuildMarkdownFontCss(StrL("New Computer Modern Sans"), {}), StrL("EnhancedMDNewCMSansMath")));
    css = BuildMarkdownFontCss(StrL("New Computer Modern Book"), StrL("New Computer Modern Sans Math"));
    utassert(str::Contains(css, StrL("EnhancedWebNewCM10-Book.woff2")));
    utassert(str::Contains(css, StrL("EnhancedWebNewCMSansMath-Regular.woff2")));
    utassert(str::Contains(css, StrL("--base-font-size:var(--enhanced-ui-font-size)!important")));
    utassert(str::Contains(MarkdownSuppliedCss(false), StrL("--color-bg: #ffffff")));
    utassert(str::Contains(MarkdownSuppliedCss(true), StrL("--color-bg: #1e1e1e")));
}
#endif
