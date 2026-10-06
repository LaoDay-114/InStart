// ============================================================
// 自动生成：多版本中介映射表（gen_mappings.py 生成，请勿手改）
// 覆盖版本: 1.21 / 1.21.1 / 1.21.2 / 1.21.3 / 1.21.4 / 1.21.5 / 1.21.6 / 1.21.7 / 1.21.8 / 1.21.9 / 1.21.10 / 1.21.11
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
    const char* clsMinecraftServer; // 内置服务端（单机伤害判定方）
    const char* clsPlayerManager;
    const char* clsAttributeContainer;
    const char* clsAttributeInstance;
    const char* clsEntityAttributes;
    const char* clsMobEntity;       // 杀戮光环排除生物判断
    const char* clsRegistryEntry;  // 属性 RegistryEntry（取 MOVEMENT_SPEED 用）
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
    // 内置服务端玩家链路
    const char* mGetServer;         // MinecraftClient.getServer()
    const char* mGetPlayerManager;  // MinecraftServer.getPlayerManager()
    const char* mGetPlayerList;     // PlayerManager.getPlayerList()
    // 移动速度属性
    const char* mGetAttributes;     // LivingEntity.getAttributes()
    const char* mAttrGet;           // AttributeContainer.get(RegistryEntry)
    const char* mSetBaseValue;      // AttributeInstance.setBaseValue(D)
    const char* fMovementSpeed;     // EntityAttributes.MOVEMENT_SPEED（旧名 GENERIC_MOVEMENT_SPEED）
    const char* fOnGround;          // Entity.onGround（FakeGround 用）
    const char* mIsSprinting;      // Entity.isSprinting（加速属性保留冲刺倍率）
};

static const McVerMap g_mcVerMaps[] = {
    { "1.21", "net/minecraft/class_310", "net/minecraft/class_746", "net/minecraft/class_315", "net/minecraft/class_7172", "net/minecraft/class_1657", "net/minecraft/class_1656", "net/minecraft/class_1297", "net/minecraft/class_638", "net/minecraft/class_1309", "net/minecraft/class_636", "net/minecraft/class_1661", "net/minecraft/class_1799", "net/minecraft/class_1792", "net/minecraft/class_1802", "net/minecraft/class_1713", "net/minecraft/class_1723", "net/minecraft/class_437", "net/minecraft/class_2371", "net/minecraft/class_1132", "net/minecraft/class_3324", "net/minecraft/class_5131", "net/minecraft/class_1324", "net/minecraft/class_5134", "net/minecraft/class_1308", "net/minecraft/class_6880", "method_1551", "method_1515", "method_7355", "method_23317", "method_23318", "method_23321", "method_41748", "method_18112", "method_5834", "method_2918", "method_2906", "method_31548", "method_6079", "method_29504", "method_7960", "method_7909", "field_7498", "field_1724", "field_1690", "field_1687", "field_1761", "field_1755", "field_7503", "field_7478", "field_7479", "field_7481", "field_7482", "field_6017", 'F', "field_1840", "field_8288", "field_7791", "field_7547", "method_1576", "method_3760", "method_14571", "method_6127", "method_45329", "method_6192", "field_23719", "field_5952", "method_5624" },
    { "1.21.1", "net/minecraft/class_310", "net/minecraft/class_746", "net/minecraft/class_315", "net/minecraft/class_7172", "net/minecraft/class_1657", "net/minecraft/class_1656", "net/minecraft/class_1297", "net/minecraft/class_638", "net/minecraft/class_1309", "net/minecraft/class_636", "net/minecraft/class_1661", "net/minecraft/class_1799", "net/minecraft/class_1792", "net/minecraft/class_1802", "net/minecraft/class_1713", "net/minecraft/class_1723", "net/minecraft/class_437", "net/minecraft/class_2371", "net/minecraft/class_1132", "net/minecraft/class_3324", "net/minecraft/class_5131", "net/minecraft/class_1324", "net/minecraft/class_5134", "net/minecraft/class_1308", "net/minecraft/class_6880", "method_1551", "method_1515", "method_7355", "method_23317", "method_23318", "method_23321", "method_41748", "method_18112", "method_5834", "method_2918", "method_2906", "method_31548", "method_6079", "method_29504", "method_7960", "method_7909", "field_7498", "field_1724", "field_1690", "field_1687", "field_1761", "field_1755", "field_7503", "field_7478", "field_7479", "field_7481", "field_7482", "field_6017", 'F', "field_1840", "field_8288", "field_7791", "field_7547", "method_1576", "method_3760", "method_14571", "method_6127", "method_45329", "method_6192", "field_23719", "field_5952", "method_5624" },
    { "1.21.2", "net/minecraft/class_310", "net/minecraft/class_746", "net/minecraft/class_315", "net/minecraft/class_7172", "net/minecraft/class_1657", "net/minecraft/class_1656", "net/minecraft/class_1297", "net/minecraft/class_638", "net/minecraft/class_1309", "net/minecraft/class_636", "net/minecraft/class_1661", "net/minecraft/class_1799", "net/minecraft/class_1792", "net/minecraft/class_1802", "net/minecraft/class_1713", "net/minecraft/class_1723", "net/minecraft/class_437", "net/minecraft/class_2371", "net/minecraft/class_1132", "net/minecraft/class_3324", "net/minecraft/class_5131", "net/minecraft/class_1324", "net/minecraft/class_5134", "net/minecraft/class_1308", "net/minecraft/class_6880", "method_1551", "method_1515", "method_7355", "method_23317", "method_23318", "method_23321", "method_41748", "method_18112", "method_5834", "method_2918", "method_2906", "method_31548", "method_6079", "method_29504", "method_7960", "method_7909", "field_7498", "field_1724", "field_1690", "field_1687", "field_1761", "field_1755", "field_7503", "field_7478", "field_7479", "field_7481", "field_7482", "field_6017", 'F', "field_1840", "field_8288", "field_7791", "field_7547", "method_1576", "method_3760", "method_14571", "method_6127", "method_45329", "method_6192", "field_23719", "field_5952", "method_5624" },
    { "1.21.3", "net/minecraft/class_310", "net/minecraft/class_746", "net/minecraft/class_315", "net/minecraft/class_7172", "net/minecraft/class_1657", "net/minecraft/class_1656", "net/minecraft/class_1297", "net/minecraft/class_638", "net/minecraft/class_1309", "net/minecraft/class_636", "net/minecraft/class_1661", "net/minecraft/class_1799", "net/minecraft/class_1792", "net/minecraft/class_1802", "net/minecraft/class_1713", "net/minecraft/class_1723", "net/minecraft/class_437", "net/minecraft/class_2371", "net/minecraft/class_1132", "net/minecraft/class_3324", "net/minecraft/class_5131", "net/minecraft/class_1324", "net/minecraft/class_5134", "net/minecraft/class_1308", "net/minecraft/class_6880", "method_1551", "method_1515", "method_7355", "method_23317", "method_23318", "method_23321", "method_41748", "method_18112", "method_5834", "method_2918", "method_2906", "method_31548", "method_6079", "method_29504", "method_7960", "method_7909", "field_7498", "field_1724", "field_1690", "field_1687", "field_1761", "field_1755", "field_7503", "field_7478", "field_7479", "field_7481", "field_7482", "field_6017", 'F', "field_1840", "field_8288", "field_7791", "field_7547", "method_1576", "method_3760", "method_14571", "method_6127", "method_45329", "method_6192", "field_23719", "field_5952", "method_5624" },
    { "1.21.4", "net/minecraft/class_310", "net/minecraft/class_746", "net/minecraft/class_315", "net/minecraft/class_7172", "net/minecraft/class_1657", "net/minecraft/class_1656", "net/minecraft/class_1297", "net/minecraft/class_638", "net/minecraft/class_1309", "net/minecraft/class_636", "net/minecraft/class_1661", "net/minecraft/class_1799", "net/minecraft/class_1792", "net/minecraft/class_1802", "net/minecraft/class_1713", "net/minecraft/class_1723", "net/minecraft/class_437", "net/minecraft/class_2371", "net/minecraft/class_1132", "net/minecraft/class_3324", "net/minecraft/class_5131", "net/minecraft/class_1324", "net/minecraft/class_5134", "net/minecraft/class_1308", "net/minecraft/class_6880", "method_1551", "method_1515", "method_7355", "method_23317", "method_23318", "method_23321", "method_41748", "method_18112", "method_5834", "method_2918", "method_2906", "method_31548", "method_6079", "method_29504", "method_7960", "method_7909", "field_7498", "field_1724", "field_1690", "field_1687", "field_1761", "field_1755", "field_7503", "field_7478", "field_7479", "field_7481", "field_7482", "field_6017", 'F', "field_1840", "field_8288", "field_7791", "field_7547", "method_1576", "method_3760", "method_14571", "method_6127", "method_45329", "method_6192", "field_23719", "field_5952", "method_5624" },
    { "1.21.5", "net/minecraft/class_310", "net/minecraft/class_746", "net/minecraft/class_315", "net/minecraft/class_7172", "net/minecraft/class_1657", "net/minecraft/class_1656", "net/minecraft/class_1297", "net/minecraft/class_638", "net/minecraft/class_1309", "net/minecraft/class_636", "net/minecraft/class_1661", "net/minecraft/class_1799", "net/minecraft/class_1792", "net/minecraft/class_1802", "net/minecraft/class_1713", "net/minecraft/class_1723", "net/minecraft/class_437", "net/minecraft/class_2371", "net/minecraft/class_1132", "net/minecraft/class_3324", "net/minecraft/class_5131", "net/minecraft/class_1324", "net/minecraft/class_5134", "net/minecraft/class_1308", "net/minecraft/class_6880", "method_1551", "method_1515", "method_7355", "method_23317", "method_23318", "method_23321", "method_41748", "method_18112", "method_5834", "method_2918", "method_2906", "method_31548", "method_6079", "method_29504", "method_7960", "method_7909", "field_7498", "field_1724", "field_1690", "field_1687", "field_1761", "field_1755", "field_7503", "field_7478", "field_7479", "field_7481", "field_7482", "field_6017", 'D', "field_1840", "field_8288", "field_7791", "field_7547", "method_1576", "method_3760", "method_14571", "method_6127", "method_45329", "method_6192", "field_23719", "field_5952", "method_5624" },
    { "1.21.6", "net/minecraft/class_310", "net/minecraft/class_746", "net/minecraft/class_315", "net/minecraft/class_7172", "net/minecraft/class_1657", "net/minecraft/class_1656", "net/minecraft/class_1297", "net/minecraft/class_638", "net/minecraft/class_1309", "net/minecraft/class_636", "net/minecraft/class_1661", "net/minecraft/class_1799", "net/minecraft/class_1792", "net/minecraft/class_1802", "net/minecraft/class_1713", "net/minecraft/class_1723", "net/minecraft/class_437", "net/minecraft/class_2371", "net/minecraft/class_1132", "net/minecraft/class_3324", "net/minecraft/class_5131", "net/minecraft/class_1324", "net/minecraft/class_5134", "net/minecraft/class_1308", "net/minecraft/class_6880", "method_1551", "method_1515", "method_7355", "method_23317", "method_23318", "method_23321", "method_41748", "method_18112", "method_5834", "method_2918", "method_2906", "method_31548", "method_6079", "method_29504", "method_7960", "method_7909", "field_7498", "field_1724", "field_1690", "field_1687", "field_1761", "field_1755", "field_7503", "field_7478", "field_7479", "field_7481", "field_7482", "field_6017", 'D', "field_1840", "field_8288", "field_7791", "field_7547", "method_1576", "method_3760", "method_14571", "method_6127", "method_45329", "method_6192", "field_23719", "field_5952", "method_5624" },
    { "1.21.7", "net/minecraft/class_310", "net/minecraft/class_746", "net/minecraft/class_315", "net/minecraft/class_7172", "net/minecraft/class_1657", "net/minecraft/class_1656", "net/minecraft/class_1297", "net/minecraft/class_638", "net/minecraft/class_1309", "net/minecraft/class_636", "net/minecraft/class_1661", "net/minecraft/class_1799", "net/minecraft/class_1792", "net/minecraft/class_1802", "net/minecraft/class_1713", "net/minecraft/class_1723", "net/minecraft/class_437", "net/minecraft/class_2371", "net/minecraft/class_1132", "net/minecraft/class_3324", "net/minecraft/class_5131", "net/minecraft/class_1324", "net/minecraft/class_5134", "net/minecraft/class_1308", "net/minecraft/class_6880", "method_1551", "method_1515", "method_7355", "method_23317", "method_23318", "method_23321", "method_41748", "method_18112", "method_5834", "method_2918", "method_2906", "method_31548", "method_6079", "method_29504", "method_7960", "method_7909", "field_7498", "field_1724", "field_1690", "field_1687", "field_1761", "field_1755", "field_7503", "field_7478", "field_7479", "field_7481", "field_7482", "field_6017", 'D', "field_1840", "field_8288", "field_7791", "field_7547", "method_1576", "method_3760", "method_14571", "method_6127", "method_45329", "method_6192", "field_23719", "field_5952", "method_5624" },
    { "1.21.8", "net/minecraft/class_310", "net/minecraft/class_746", "net/minecraft/class_315", "net/minecraft/class_7172", "net/minecraft/class_1657", "net/minecraft/class_1656", "net/minecraft/class_1297", "net/minecraft/class_638", "net/minecraft/class_1309", "net/minecraft/class_636", "net/minecraft/class_1661", "net/minecraft/class_1799", "net/minecraft/class_1792", "net/minecraft/class_1802", "net/minecraft/class_1713", "net/minecraft/class_1723", "net/minecraft/class_437", "net/minecraft/class_2371", "net/minecraft/class_1132", "net/minecraft/class_3324", "net/minecraft/class_5131", "net/minecraft/class_1324", "net/minecraft/class_5134", "net/minecraft/class_1308", "net/minecraft/class_6880", "method_1551", "method_1515", "method_7355", "method_23317", "method_23318", "method_23321", "method_41748", "method_18112", "method_5834", "method_2918", "method_2906", "method_31548", "method_6079", "method_29504", "method_7960", "method_7909", "field_7498", "field_1724", "field_1690", "field_1687", "field_1761", "field_1755", "field_7503", "field_7478", "field_7479", "field_7481", "field_7482", "field_6017", 'D', "field_1840", "field_8288", "field_7791", "field_7547", "method_1576", "method_3760", "method_14571", "method_6127", "method_45329", "method_6192", "field_23719", "field_5952", "method_5624" },
    { "1.21.9", "net/minecraft/class_310", "net/minecraft/class_746", "net/minecraft/class_315", "net/minecraft/class_7172", "net/minecraft/class_1657", "net/minecraft/class_1656", "net/minecraft/class_1297", "net/minecraft/class_638", "net/minecraft/class_1309", "net/minecraft/class_636", "net/minecraft/class_1661", "net/minecraft/class_1799", "net/minecraft/class_1792", "net/minecraft/class_1802", "net/minecraft/class_1713", "net/minecraft/class_1723", "net/minecraft/class_437", "net/minecraft/class_2371", "net/minecraft/class_1132", "net/minecraft/class_3324", "net/minecraft/class_5131", "net/minecraft/class_1324", "net/minecraft/class_5134", "net/minecraft/class_1308", "net/minecraft/class_6880", "method_1551", "method_1515", "method_7355", "method_23317", "method_23318", "method_23321", "method_41748", "method_18112", "method_5834", "method_2918", "method_2906", "method_31548", "method_6079", "method_29504", "method_7960", "method_7909", "field_7498", "field_1724", "field_1690", "field_1687", "field_1761", "field_1755", "field_7503", "field_7478", "field_7479", "field_7481", "field_7482", "field_6017", 'D', "field_1840", "field_8288", "field_7791", "field_7547", "method_1576", "method_3760", "method_14571", "method_6127", "method_45329", "method_6192", "field_23719", "field_5952", "method_5624" },
    { "1.21.10", "net/minecraft/class_310", "net/minecraft/class_746", "net/minecraft/class_315", "net/minecraft/class_7172", "net/minecraft/class_1657", "net/minecraft/class_1656", "net/minecraft/class_1297", "net/minecraft/class_638", "net/minecraft/class_1309", "net/minecraft/class_636", "net/minecraft/class_1661", "net/minecraft/class_1799", "net/minecraft/class_1792", "net/minecraft/class_1802", "net/minecraft/class_1713", "net/minecraft/class_1723", "net/minecraft/class_437", "net/minecraft/class_2371", "net/minecraft/class_1132", "net/minecraft/class_3324", "net/minecraft/class_5131", "net/minecraft/class_1324", "net/minecraft/class_5134", "net/minecraft/class_1308", "net/minecraft/class_6880", "method_1551", "method_1515", "method_7355", "method_23317", "method_23318", "method_23321", "method_41748", "method_18112", "method_5834", "method_2918", "method_2906", "method_31548", "method_6079", "method_29504", "method_7960", "method_7909", "field_7498", "field_1724", "field_1690", "field_1687", "field_1761", "field_1755", "field_7503", "field_7478", "field_7479", "field_7481", "field_7482", "field_6017", 'D', "field_1840", "field_8288", "field_7791", "field_7547", "method_1576", "method_3760", "method_14571", "method_6127", "method_45329", "method_6192", "field_23719", "field_5952", "method_5624" },
    { "1.21.11", "net/minecraft/class_310", "net/minecraft/class_746", "net/minecraft/class_315", "net/minecraft/class_7172", "net/minecraft/class_1657", "net/minecraft/class_1656", "net/minecraft/class_1297", "net/minecraft/class_638", "net/minecraft/class_1309", "net/minecraft/class_636", "net/minecraft/class_1661", "net/minecraft/class_1799", "net/minecraft/class_1792", "net/minecraft/class_1802", "net/minecraft/class_1713", "net/minecraft/class_1723", "net/minecraft/class_437", "net/minecraft/class_2371", "net/minecraft/class_1132", "net/minecraft/class_3324", "net/minecraft/class_5131", "net/minecraft/class_1324", "net/minecraft/class_5134", "net/minecraft/class_1308", "net/minecraft/class_6880", "method_1551", "method_1515", "method_7355", "method_23317", "method_23318", "method_23321", "method_41748", "method_18112", "method_5834", "method_2918", "method_2906", "method_31548", "method_6079", "method_29504", "method_7960", "method_7909", "field_7498", "field_1724", "field_1690", "field_1687", "field_1761", "field_1755", "field_7503", "field_7478", "field_7479", "field_7481", "field_7482", "field_6017", 'D', "field_1840", "field_8288", "field_7791", "field_7547", "method_1576", "method_3760", "method_14571", "method_6127", "method_45329", "method_6192", "field_23719", "field_5952", "method_5624" },
};

static const int g_mcVerMapsCount = (int)(sizeof(g_mcVerMaps) / sizeof(g_mcVerMaps[0]));

// ---- 引导探测：版本识别前先定位 MinecraftClient / getInstance / getGameVersion ----
// 各版本取并集，运行时逐个尝试
static const char* const g_bootClsMc[] = { "net/minecraft/class_310", nullptr };
static const char* const g_bootMGetInstance[] = { "method_1551", nullptr };
static const char* const g_bootMGetGameVersion[] = { "method_1515", nullptr };
