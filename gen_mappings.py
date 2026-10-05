# ============================================================
# InStart 多版本映射生成器
# 为 1.21 ~ 1.21.11 各版本下载 yarn 映射，提取所需中介(intermediary) ID，
# 生成「版本 -> ID 表」结构的 src/jni/mappings.h（mc.cpp 运行时按版本查表）
# 用法: python gen_mappings.py
# ============================================================
import io
import json
import os
import urllib.request
import zipfile

VERSIONS = [
    "1.21", "1.21.1", "1.21.2", "1.21.3", "1.21.4", "1.21.5",
    "1.21.6", "1.21.7", "1.21.8", "1.21.9", "1.21.10", "1.21.11",
]

BASE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(BASE, "mappings")
OUT = os.path.join(BASE, "src", "jni", "mappings.h")

# ---- 需要的类: yarn(named) 全名 ----
WANT_CLASSES = {
    "net/minecraft/client/MinecraftClient",
    "net/minecraft/client/network/ClientPlayerEntity",
    "net/minecraft/client/option/GameOptions",
    "net/minecraft/client/option/SimpleOption",
    "net/minecraft/entity/player/PlayerEntity",
    "net/minecraft/entity/player/PlayerAbilities",
    "net/minecraft/entity/Entity",
    "net/minecraft/client/world/ClientWorld",
    "net/minecraft/entity/LivingEntity",
    "net/minecraft/client/network/ClientPlayerInteractionManager",
    "net/minecraft/entity/player/PlayerInventory",
    "net/minecraft/item/ItemStack",
    "net/minecraft/item/Item",
    "net/minecraft/item/Items",
    "net/minecraft/screen/slot/SlotActionType",
    "net/minecraft/screen/PlayerScreenHandler",
    "net/minecraft/client/gui/screen/Screen",
    "net/minecraft/util/collection/DefaultedList",
}

# ---- 需要的成员: (yarn类全名, f/m, yarn成员名, 描述符过滤或None) ----
# desc 为 official 命名空间的描述符；基本类型与 java/* 类型跨版本稳定，可作过滤条件
WANT_MEMBERS = [
    ("net/minecraft/client/MinecraftClient", "f", "player", None),
    ("net/minecraft/client/MinecraftClient", "f", "options", None),
    ("net/minecraft/client/MinecraftClient", "f", "world", None),
    ("net/minecraft/client/MinecraftClient", "m", "getInstance", None),
    ("net/minecraft/client/MinecraftClient", "m", "getGameVersion", "()Ljava/lang/String;"),
    ("net/minecraft/entity/player/PlayerEntity", "f", "abilities", None),
    ("net/minecraft/entity/player/PlayerEntity", "m", "sendAbilitiesUpdate", "()V"),
    ("net/minecraft/entity/player/PlayerAbilities", "f", "allowFlying", "Z"),
    ("net/minecraft/entity/player/PlayerAbilities", "f", "flying", "Z"),
    ("net/minecraft/entity/player/PlayerAbilities", "f", "flySpeed", "F"),
    ("net/minecraft/entity/player/PlayerAbilities", "f", "walkSpeed", "F"),
    ("net/minecraft/entity/Entity", "m", "getX", "()D"),
    ("net/minecraft/entity/Entity", "m", "getY", "()D"),
    ("net/minecraft/entity/Entity", "m", "getZ", "()D"),
    ("net/minecraft/entity/Entity", "m", "setGlowing", "(Z)V"),
    ("net/minecraft/entity/Entity", "f", "fallDistance", None),  # 1.21.8- 为 F，1.21.9+ 为 D
    ("net/minecraft/client/option/GameOptions", "f", "gamma", None),
    ("net/minecraft/client/option/SimpleOption", "m", "setValue", "(Ljava/lang/Object;)V"),
    ("net/minecraft/client/world/ClientWorld", "m", "getEntities", "()Ljava/lang/Iterable;"),
    ("net/minecraft/client/MinecraftClient", "f", "interactionManager", None),
    ("net/minecraft/client/MinecraftClient", "f", "currentScreen", None),
    ("net/minecraft/client/network/ClientPlayerInteractionManager", "m", "attackEntity", None),
    ("net/minecraft/client/network/ClientPlayerInteractionManager", "m", "clickSlot", None),
    ("net/minecraft/entity/player/PlayerEntity", "m", "getInventory", None),
    ("net/minecraft/entity/player/PlayerEntity", "f", "playerScreenHandler", None),
    ("net/minecraft/entity/player/PlayerInventory", "f", "main", None),
    ("net/minecraft/entity/LivingEntity", "m", "getOffHandStack", None),
    ("net/minecraft/entity/LivingEntity", "m", "isDeadOrDying", "()Z"),
    ("net/minecraft/entity/LivingEntity", "m", "isDead", "()Z"),  # 旧版名称（1.21）
    ("net/minecraft/item/ItemStack", "m", "isEmpty", "()Z"),
    ("net/minecraft/item/ItemStack", "m", "getItem", None),
    ("net/minecraft/item/Items", "f", "TOTEM_OF_UNDYING", None),
    ("net/minecraft/screen/slot/SlotActionType", "f", "SWAP", None),
]


def fetch(url, binary=False):
    req = urllib.request.Request(url, headers={"User-Agent": "InStart-Mapper"})
    with urllib.request.urlopen(req, timeout=120) as r:
        data = r.read()
    return data if binary else data.decode("utf-8")


def yarn_build(mcver):
    arr = json.loads(fetch(f"https://meta.fabricmc.net/v2/versions/yarn/{mcver}"))
    if not arr:
        raise RuntimeError(f"{mcver} 没有可用的 yarn 构建")
    return arr[0]["version"]  # 最新 build


def get_tiny(mcver):
    """下载并缓存该版本的 mappings.tiny 文本"""
    os.makedirs(CACHE, exist_ok=True)
    cache = os.path.join(CACHE, f"yarn-{mcver}.tiny")
    if os.path.exists(cache):
        return open(cache, encoding="utf-8").read()
    yv = yarn_build(mcver)
    enc = yv.replace("+", "%2B")
    url = f"https://maven.fabricmc.net/net/fabricmc/yarn/{enc}/yarn-{enc}-mergedv2.jar"
    print(f"  下载 {mcver} (yarn {yv}) ...")
    jar = fetch(url, binary=True)
    text = zipfile.ZipFile(io.BytesIO(jar)).read("mappings/mappings.tiny").decode("utf-8")
    with open(cache, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return text


def parse_tiny(text):
    """返回 (classes, members)
    classes: named类名 -> intermediary类名
    members: (named类名, kind, named成员名) -> [(intermediary, official_desc), ...]
    """
    lines = text.splitlines()
    header = lines[0].split("\t")
    ns = header[3:]
    idx_named, idx_int = ns.index("named"), ns.index("intermediary")
    classes, members = {}, {}
    cur = None
    for ln in lines[1:]:
        p = ln.split("\t")
        if p[0] == "c":
            cur = p[1 + idx_named]
            if cur in WANT_CLASSES:
                classes[cur] = p[1 + idx_int]
        elif p[0] == "" and len(p) > 3 and p[1] in ("f", "m") and cur:
            kind, desc = p[1], p[2]
            cols = p[3:]
            key = (cur, kind, cols[idx_named])
            members.setdefault(key, []).append((cols[idx_int], desc))
    return classes, members


def pick(cands, desc_filter, what):
    """按描述符过滤挑一个候选；filter 为 None 时取第一个"""
    for inter, desc in cands:
        if desc_filter is None or desc == desc_filter:
            return inter, desc
    raise RuntimeError(f"未找到成员: {what}")


def build_record(mcver, classes, members):
    """按 mappings.h 中 McVerMap 的字段顺序组装一条版本记录"""
    for c in WANT_CLASSES:
        if c not in classes:
            raise RuntimeError(f"{mcver} 缺少类 {c}")

    def cls(named):
        return classes[named]

    def mem(named_cls, kind, name):
        key = (named_cls, kind, name)
        filt = next(f for (c, k, n, f) in WANT_MEMBERS if c == named_cls and k == kind and n == name)
        if key not in members:
            raise RuntimeError(f"{mcver} 缺少成员 {named_cls}.{name}")
        return pick(members[key], filt, f"{mcver} {named_cls}.{name}")

    rec = {
        "version": mcver,
        "clsMinecraftClient": cls("net/minecraft/client/MinecraftClient"),
        "clsClientPlayerEntity": cls("net/minecraft/client/network/ClientPlayerEntity"),
        "clsGameOptions": cls("net/minecraft/client/option/GameOptions"),
        "clsSimpleOption": cls("net/minecraft/client/option/SimpleOption"),
        "clsPlayerEntity": cls("net/minecraft/entity/player/PlayerEntity"),
        "clsPlayerAbilities": cls("net/minecraft/entity/player/PlayerAbilities"),
        "clsEntity": cls("net/minecraft/entity/Entity"),
        "clsClientWorld": cls("net/minecraft/client/world/ClientWorld"),
        "clsLivingEntity": cls("net/minecraft/entity/LivingEntity"),
        "clsInteractionManager": cls("net/minecraft/client/network/ClientPlayerInteractionManager"),
        "clsPlayerInventory": cls("net/minecraft/entity/player/PlayerInventory"),
        "clsItemStack": cls("net/minecraft/item/ItemStack"),
        "clsItem": cls("net/minecraft/item/Item"),
        "clsItems": cls("net/minecraft/item/Items"),
        "clsSlotActionType": cls("net/minecraft/screen/slot/SlotActionType"),
        "clsPlayerScreenHandler": cls("net/minecraft/screen/PlayerScreenHandler"),
        "clsScreen": cls("net/minecraft/client/gui/screen/Screen"),
        "clsDefaultedList": cls("net/minecraft/util/collection/DefaultedList"),
        "mGetInstance": mem("net/minecraft/client/MinecraftClient", "m", "getInstance")[0],
        "mGetGameVersion": mem("net/minecraft/client/MinecraftClient", "m", "getGameVersion")[0],
        "mSendAbilitiesUpdate": mem("net/minecraft/entity/player/PlayerEntity", "m", "sendAbilitiesUpdate")[0],
        "mGetX": mem("net/minecraft/entity/Entity", "m", "getX")[0],
        "mGetY": mem("net/minecraft/entity/Entity", "m", "getY")[0],
        "mGetZ": mem("net/minecraft/entity/Entity", "m", "getZ")[0],
        "mSetValue": mem("net/minecraft/client/option/SimpleOption", "m", "setValue")[0],
        "mGetEntities": mem("net/minecraft/client/world/ClientWorld", "m", "getEntities")[0],
        "mSetGlowing": mem("net/minecraft/entity/Entity", "m", "setGlowing")[0],
        "mAttackEntity": mem("net/minecraft/client/network/ClientPlayerInteractionManager", "m", "attackEntity")[0],
        "mClickSlot": mem("net/minecraft/client/network/ClientPlayerInteractionManager", "m", "clickSlot")[0],
        "mGetInventory": mem("net/minecraft/entity/player/PlayerEntity", "m", "getInventory")[0],
        "fPlayerScreenHandler": mem("net/minecraft/entity/player/PlayerEntity", "f", "playerScreenHandler")[0],
        "fInvMain": mem("net/minecraft/entity/player/PlayerInventory", "f", "main")[0],
        "mGetOffHandStack": mem("net/minecraft/entity/LivingEntity", "m", "getOffHandStack")[0],
        # isDeadOrDying：1.21 中名为 isDead，取两者中存在的那个
        "mDeadOrDying": pick(
            members.get(("net/minecraft/entity/LivingEntity", "m", "isDeadOrDying"),
                        members[("net/minecraft/entity/LivingEntity", "m", "isDead")]),
            "()Z", f"{mcver} LivingEntity.isDeadOrDying")[0],
        "mStackIsEmpty": mem("net/minecraft/item/ItemStack", "m", "isEmpty")[0],
        "mStackGetItem": mem("net/minecraft/item/ItemStack", "m", "getItem")[0],
        "fMcPlayer": mem("net/minecraft/client/MinecraftClient", "f", "player")[0],
        "fMcOptions": mem("net/minecraft/client/MinecraftClient", "f", "options")[0],
        "fMcWorld": mem("net/minecraft/client/MinecraftClient", "f", "world")[0],
        "fMcInteractionManager": mem("net/minecraft/client/MinecraftClient", "f", "interactionManager")[0],
        "fMcCurrentScreen": mem("net/minecraft/client/MinecraftClient", "f", "currentScreen")[0],
        "fPlayerAbilities": mem("net/minecraft/entity/player/PlayerEntity", "f", "abilities")[0],
        "fAllowFlying": mem("net/minecraft/entity/player/PlayerAbilities", "f", "allowFlying")[0],
        "fFlying": mem("net/minecraft/entity/player/PlayerAbilities", "f", "flying")[0],
        "fFlySpeed": mem("net/minecraft/entity/player/PlayerAbilities", "f", "flySpeed")[0],
        "fWalkSpeed": mem("net/minecraft/entity/player/PlayerAbilities", "f", "walkSpeed")[0],
        "fGamma": mem("net/minecraft/client/option/GameOptions", "f", "gamma")[0],
        "fItemsTotem": mem("net/minecraft/item/Items", "f", "TOTEM_OF_UNDYING")[0],
        "fSlotSwap": mem("net/minecraft/screen/slot/SlotActionType", "f", "SWAP")[0],
    }
    inter, desc = mem("net/minecraft/entity/Entity", "f", "fallDistance")
    rec["fFallDistance"] = inter
    rec["fallDistType"] = desc[0]  # 'F' 或 'D'
    return rec


# 与 McVerMap 结构体完全一致的字段顺序
FIELD_ORDER = [
    "version",
    "clsMinecraftClient", "clsClientPlayerEntity", "clsGameOptions", "clsSimpleOption",
    "clsPlayerEntity", "clsPlayerAbilities", "clsEntity", "clsClientWorld", "clsLivingEntity",
    "clsInteractionManager", "clsPlayerInventory", "clsItemStack", "clsItem", "clsItems",
    "clsSlotActionType", "clsPlayerScreenHandler", "clsScreen", "clsDefaultedList",
    "mGetInstance", "mGetGameVersion", "mSendAbilitiesUpdate",
    "mGetX", "mGetY", "mGetZ", "mSetValue", "mGetEntities", "mSetGlowing",
    "mAttackEntity", "mClickSlot", "mGetInventory", "mGetOffHandStack", "mDeadOrDying",
    "mStackIsEmpty", "mStackGetItem", "fPlayerScreenHandler",
    "fMcPlayer", "fMcOptions", "fMcWorld", "fMcInteractionManager", "fMcCurrentScreen",
    "fPlayerAbilities",
    "fAllowFlying", "fFlying", "fFlySpeed", "fWalkSpeed",
    "fFallDistance", "fallDistType", "fGamma",
    "fItemsTotem", "fSlotSwap", "fInvMain",
]

HEADER = """// ============================================================
// 自动生成：多版本中介映射表（gen_mappings.py 生成，请勿手改）
// 覆盖版本: %s
// ============================================================
#pragma once

struct McVerMap {
    const char* version;            // 游戏版本号，如 "1.21.11"
    // ---- 类（intermediary）----
    const char* clsMinecraftClient;
    const char* clsClientPlayerEntity;
    const char* clsGameOptions;
    const char* clsSimpleOption;
    const char* clsPlayerEntity;
    const char* clsPlayerAbilities;
    const char* clsEntity;
    const char* clsClientWorld;
    const char* clsLivingEntity;
    const char* clsInteractionManager;  // ClientPlayerInteractionManager
    const char* clsPlayerInventory;
    const char* clsItemStack;
    const char* clsItem;
    const char* clsItems;
    const char* clsSlotActionType;
    const char* clsPlayerScreenHandler; // 用于 fPlayerScreenHandler 字段描述符
    const char* clsScreen;              // 用于 fMcCurrentScreen 字段描述符
    const char* clsDefaultedList;   // PlayerInventory.main 的声明类型
    // ---- 方法（intermediary 名）----
    const char* mGetInstance;       // MinecraftClient.getInstance()
    const char* mGetGameVersion;    // MinecraftClient.getGameVersion()
    const char* mSendAbilitiesUpdate;
    const char* mGetX;
    const char* mGetY;
    const char* mGetZ;
    const char* mSetValue;          // SimpleOption.setValue(Object)
    const char* mGetEntities;       // ClientWorld.getEntities()
    const char* mSetGlowing;        // Entity.setGlowing(Z)
    const char* mAttackEntity;      // InteractionManager.attackEntity(player, entity)
    const char* mClickSlot;         // InteractionManager.clickSlot(...)
    const char* mGetInventory;      // PlayerEntity.getInventory()
    const char* mGetOffHandStack;   // LivingEntity.getOffHandStack()
    const char* mDeadOrDying;       // LivingEntity.isDeadOrDying()
    const char* mStackIsEmpty;      // ItemStack.isEmpty()
    const char* mStackGetItem;      // ItemStack.getItem()
    const char* fPlayerScreenHandler; // PlayerEntity.playerScreenHandler（其 syncId 恒为 0）
    // ---- 字段（intermediary 名）----
    const char* fMcPlayer;
    const char* fMcOptions;
    const char* fMcWorld;
    const char* fMcInteractionManager;
    const char* fMcCurrentScreen;   // MinecraftClient.currentScreen（null = 未打开 GUI）
    const char* fPlayerAbilities;
    const char* fAllowFlying;
    const char* fFlying;
    const char* fFlySpeed;
    const char* fWalkSpeed;
    const char* fFallDistance;
    char        fallDistType;       // 'F'(float, <=1.21.8) 或 'D'(double, 1.21.9+)
    const char* fGamma;
    const char* fItemsTotem;        // Items.TOTEM_OF_UNDYING（静态字段）
    const char* fSlotSwap;          // SlotActionType.SWAP（静态枚举字段）
    const char* fInvMain;           // PlayerInventory.main（List<ItemStack>）
};

static const McVerMap g_mcVerMaps[] = {
"""

FOOTER = """};

static const int g_mcVerMapsCount = (int)(sizeof(g_mcVerMaps) / sizeof(g_mcVerMaps[0]));

// ---- 引导探测：版本识别前先定位 MinecraftClient / getInstance / getGameVersion ----
// 各版本取并集，运行时逐个尝试
static const char* const g_bootClsMc[] = { %s, nullptr };
static const char* const g_bootMGetInstance[] = { %s, nullptr };
static const char* const g_bootMGetGameVersion[] = { %s, nullptr };
"""


def cpp_str(s):
    return '"%s"' % s


def main():
    records = []
    for v in VERSIONS:
        print(f"[{v}]")
        tiny = get_tiny(v)
        classes, members = parse_tiny(tiny)
        records.append(build_record(v, classes, members))

    # 生成版本表
    out = [HEADER % " / ".join(VERSIONS)]
    for rec in records:
        cells = []
        for k in FIELD_ORDER:
            val = rec[k]
            cells.append("'%s'" % val if k == "fallDistType" else cpp_str(val))
        out.append("    { %s },\n" % ", ".join(cells))

    # 引导变体并集（保持顺序去重）
    def uniq(key):
        seen = []
        for rec in records:
            if rec[key] not in seen:
                seen.append(rec[key])
        return ", ".join(cpp_str(x) for x in seen)

    out.append(FOOTER % (uniq("clsMinecraftClient"), uniq("mGetInstance"), uniq("mGetGameVersion")))

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(out))
    print(f"已生成 {OUT}（{len(records)} 个版本）")


if __name__ == "__main__":
    main()
