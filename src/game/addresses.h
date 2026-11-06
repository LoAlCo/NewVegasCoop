// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#pragma once

#include <cstdint>

namespace game::addr
{
    constexpr uintptr_t kThePlayer = 0x011DEA3C;
    constexpr uintptr_t kFormMap = 0x011C54C0;
    constexpr uintptr_t kFormHeapFree = 0x00401030;
    constexpr uintptr_t kFormHeapAllocate = 0x00401000;
    constexpr uintptr_t kActorSetWeaponOut = 0x008A6840;
    constexpr uintptr_t kMobileObjectJump = 0x00930640;
    constexpr int kActorReloadSlot = 0x3E8 / 4;
    constexpr uintptr_t kExtraListGetByType = 0x00410220;
    constexpr uintptr_t kExtraListAdd = 0x0040FF60;
    constexpr uintptr_t kCompassTargetSet = 0x00966F20;
    constexpr uintptr_t kCompassTargetRemove = 0x00967350;
    constexpr uintptr_t kContainerMenu = 0x011D93F8;
    constexpr uintptr_t kBarterMenu = 0x011D8FA4;
    constexpr uintptr_t kSleepWaitMenu = 0x011DA920;
    constexpr uintptr_t kSleepWaitClickSlot = 0x010763B8;
    constexpr uintptr_t kSleepWaitClick = 0x007C0220;
    constexpr uintptr_t kSleepWaitHoursValue = 0x011DA918;
    constexpr uintptr_t kShowSleepWaitMenu = 0x007BFC30;
    constexpr uintptr_t kTileGetValueFloat = 0x00A011B0;
    constexpr uintptr_t kTileSetValueFloat = 0x00A012D0;
    constexpr uintptr_t kLoadSave = 0x00850760;
    constexpr uintptr_t kStartMenuOptions = 0x011DAB88;
    constexpr uintptr_t kMakeStartMenuOptionsCall = 0x007CB886;
    constexpr uintptr_t kMakeStartMenuOptions = 0x007CC6E0;
    constexpr uintptr_t kStartMenuOptionInit = 0x007CE620;
    constexpr uintptr_t kSimpleArrayAppend = 0x007CB2E0;
    constexpr uintptr_t kLoadOption = 0x007D0680;
    constexpr uintptr_t kExitGameOption = 0x007D0BF0;
    constexpr uintptr_t kExplosionInitSlot = 0x0108F108;
    constexpr uintptr_t kExplosionInit = 0x009ACD60;
    constexpr uint32_t kExplosionSource = 0xC8;
    constexpr uint32_t kExplosionProcess = 0x68;
    constexpr uintptr_t kAlwaysActiveSetting = 0x011DEED8;
    constexpr uintptr_t kGameTimeGlobals = 0x011DE7B8;
    constexpr uint32_t kGameTimeReload = 0x1C;
    constexpr uintptr_t kMessageMenu = 0x011DA4F0;
    constexpr uint32_t kMessageMenuText = 0x34;
    constexpr uint32_t kMessageMenuButtons = 0x40;
    constexpr uintptr_t kTileSetString = 0x00A01350;
    constexpr uintptr_t kTileGetValue = 0x00A01000;
    constexpr uint32_t kTileValueString = 0xFC4;
    constexpr uintptr_t kInterfaceManager = 0x011D8A80;
    constexpr uintptr_t kNiAVObjectUpdate = 0x00A59C60;
    constexpr uintptr_t kDisplayHeightSetting = 0x011C718C, kMenuUnitsPerPixel = 0x011D8A48;
    constexpr uint32_t kMenuStack = 0x114;
    constexpr int kMenuStackSlots = 10;
    constexpr uintptr_t kMessageBoxBusy = 0x011DA4EC;
    constexpr uintptr_t kShowMessageBox = 0x00703E80;
    constexpr uintptr_t kTakeMessageBoxButton = 0x00703FA0;
    constexpr uintptr_t kDataHandler = 0x011C3F2C;
    constexpr uintptr_t kQuestObjectiveVtbl = 0x01047088;
    constexpr uintptr_t kIsInventoryObjectType = 0x00481F30;
    constexpr uintptr_t kActorReload = 0x008A8420;
    constexpr uintptr_t kAnimGroupInfos = 0x011977D8;
    constexpr uint32_t kAnimGroupCount = 245;
    constexpr uint32_t kAnimGroupInfoSize = 0x24;
    constexpr uintptr_t kProcessManager = 0x011E0E80;
    constexpr uintptr_t kTopicInfoRunResultScript = 0x0061F170;
    constexpr uintptr_t kTopicInfoGetResultScript = 0x0061EB60;
    constexpr uintptr_t kDialogMenu = 0x011D9510;
    constexpr uintptr_t kShowSubtitle = 0x00705210;
    constexpr int kProcessSaySlot = 0x2A4 / 4;
    constexpr uintptr_t kPickLineCall = 0x0061B361;
    constexpr uintptr_t kRadioSetOn = 0x008324E0;
    constexpr uintptr_t kRadioSetStation = 0x00832240;
    constexpr uintptr_t kRadioOn = 0x011DD434;
    constexpr uintptr_t kPipboyRadioStation = 0x011DD42C;
    constexpr uintptr_t kActiveRadioStations = 0x011DD554;
    constexpr uintptr_t kActorFaceAnimData = 0x008ADCB0;
    constexpr uintptr_t kSoundHandleSeek = 0x00AD8F70;
    constexpr uintptr_t kDialogueItemFirstResponse = 0x0083C7B0;
    constexpr uintptr_t kDialogueItemNextResponse = 0x0083C7E0;
    constexpr uintptr_t kDialogueItemCurrentResponse = 0x0083C820;
    constexpr uintptr_t kSoundHandleSetEndCallback = 0x00AD8E60;
    constexpr uintptr_t kSpeechEnded = 0x00936A20;
    constexpr uintptr_t kSayEnded = 0x005790F0;
    constexpr uintptr_t kSoundHandleStop = 0x00AD88F0;
    constexpr uintptr_t kSoundHandleIsPlaying = 0x00AD8930;
    constexpr uintptr_t kHudMainMenu = 0x011D96C0;
    constexpr uintptr_t kSettingGeneralSubtitles = 0x011D8928;
    constexpr uintptr_t kSettingDialogueSubtitles = 0x011D8934;
    constexpr uintptr_t kSettingExteriorCellBuffer = 0x011C3C90;
    constexpr uintptr_t kSetActorBaseFlag = 0x0047DD50;
    constexpr uint32_t kRaceFlags = 0x70;
    constexpr uint32_t kRacePlayable = 0x01;
    constexpr uint32_t kRaceChild = 0x04;
    constexpr uintptr_t kRemakeHead = 0x008D3FA0;
    constexpr uintptr_t kLoadFaceGenEgt = 0x011D5AE0;
    constexpr uintptr_t kSetTimeMultiplier = 0x00AA4DB0;
    constexpr uintptr_t kInMenuMode = 0x00702360;
    constexpr uintptr_t kPauseMenuUp = 0x004A4040;
    constexpr uintptr_t kMenuModeCalls[] = {0x0086E79E, 0x0086EEAF, 0x008898BD, 0x0086A4A1, 0x0086A4DE, 0x0063E0F8,
        0x00837D05, 0x008387C5, 0x0089C6CA, 0x0089C74A, 0x0089CEBA, 0x0089CF56, 0x008A7EA9, 0x008D1AA1};
    constexpr uintptr_t kTimeGlobal = 0x011F6394;
    constexpr uintptr_t kVatsTimeCalls[] = {0x009C7E7D, 0x009C7EA9, 0x009C7EC6, 0x009C852D, 0x009C8565};
    constexpr uintptr_t kLevelUpSkillPoints = 0x00648BC0;
    constexpr uintptr_t kLevelUpSkillPointsCall = 0x00784EE3;
    constexpr uintptr_t kMineReactsToTarget = 0x009C3930;
    constexpr uintptr_t kMineReactsCall = 0x009C3BD1;
    constexpr uintptr_t kProjectileTurnOff = 0x009C43E0;
    constexpr uintptr_t kPlayerCombatCount = 0x00953C20;
    constexpr uintptr_t kLevelUpCombatCalls[] = {0x0077C58C, 0x0077D6BE};
    constexpr uintptr_t kTakeOffItem = 0x004C0CF0;
    constexpr uintptr_t kRemoveAllTakeOffCall = 0x004CE913;
}

namespace game::mover
{
    constexpr uintptr_t kActorMoverTable = 0x0109223C;
    constexpr uintptr_t kPlayerMoverTable = 0x0109297C;
    constexpr int kSetFlagsSlot = 3;
    constexpr uintptr_t kActorSetFlags = 0x009DE060;
    constexpr uintptr_t kPlayerSetFlags = 0x009EA3E0;
    constexpr int kReleaseFlagsSlot = 4;
    constexpr uintptr_t kReleaseFlags = 0x009DE080;
    constexpr int kResetSlot = 14;
    constexpr uintptr_t kActorReset = 0x009DFB80;
    constexpr int kGetFlagsSlot = 8;
    constexpr uintptr_t kActorGetFlags = 0x009DDB70;
    constexpr uintptr_t kPlayerGetFlags = 0x009185E0;
    constexpr uint32_t kActor = 0x28;
    constexpr uint32_t kAskedFlags = 0x38;
    constexpr uint32_t kFlags = 0x3C;
    constexpr uint32_t kAskedOn = 0x71;
    constexpr uint32_t kPlayerFlags = 0x94;
}

namespace game::process
{
    constexpr uint32_t kGetAnimDataOffset = 0x1B8;
    constexpr uint32_t kLevel = 0x28;
    constexpr uint32_t kCachedValues = 0x2C;
    constexpr uint32_t kCachedWalkSpeed = 0x38;
    constexpr uint32_t kCachedRunSpeed = 0x3C;
    constexpr uint32_t kCachedFlags = 0x44;
    constexpr uint32_t kAnimActor = 0x04;
    constexpr uint32_t kAnimGroupIds = 0x4C;
    constexpr uint32_t kAnimIdle = 0x124;
    constexpr uint32_t kIdleForm = 0x2C;
    constexpr uint32_t kIdleActor = 0x34;
    constexpr uint32_t kAnimSequences = 0xE0;
    constexpr uint32_t kSequenceName = 0x08;
    constexpr int kWeaponLayer = 4;
    constexpr uint32_t kAnimController = 0xD8;
    constexpr uint32_t kControllerSequences = 0x38;
    constexpr uint32_t kControllerSequenceSlots = 0x3E;
    constexpr uint32_t kSequenceState = 0x44;
    constexpr uint32_t kSequenceCycle = 0x24;
    constexpr uint32_t kSequenceDeactivate = 0x8C;
    constexpr int kAnimLayers = 8;
    constexpr uint32_t kManagerObjects = 0x08;
    constexpr uint32_t kManagerCapacity = 0x0C;
    constexpr uint32_t kManagerObjectCount = 0x0E;
    constexpr uint32_t kManagerBegin = 0x14;
    constexpr uint32_t kManagerEnd = 0x24;
}

namespace game::opcodes
{
    constexpr uint16_t kOpensScreen[] = {
        0x1056,
        0x10D9,
        0x10DA,
        0x10DC,
        0x1173,
        0x1179,
        0x117A,
        0x1197,
        0x11C8,
        0x11D3,
        0x11E3,
        0x11E8,
        0x11F1,
        0x11F2,
        0x1244,
        0x1247,
        0x1250,
        0x1251,
        0x1253,
        0x1255,
        0x125A,
        0x1268,
        0x1279,
    };
    constexpr uint16_t kOnReference = 0x1C;
}

namespace game::ids
{
    constexpr uint32_t kDefaultFollowPlayerClose = 0x00119230;
    constexpr uint32_t kBennyIntroQuest = 0x00102037, kTutorialQuest = 0x00104C1C, kSunnyQuest = 0x0010A214, kReviseQuest = 0x0011649E;
    constexpr uint32_t kDocsDoor = 0x00103E61, kReviseActivator = 0x001164A3;
    constexpr uint32_t kPipBoy = 0x00015038, kPipBoyGlove = 0x00025B83, kVaultSuit21 = 0x00104184,
        kCourierNote = 0x00105BCD;
    constexpr uint32_t kTutorialFades[] = {0x00120E95, 0x00120E96, 0x0016A8A2, 0x0016A8A3, 0x000AC03C, 0x0011E6E1};
    struct DlcPopups { const char* file; uint32_t broadcast, loaded; };
    constexpr DlcPopups kDlcPopups[] = {{"DeadMoney.esm", 0x0062B2, 0x00F41E}, {"HonestHearts.esm", 0x008A33, 0x008A34},
        {"OldWorldBlues.esm", 0x002FD3, 0x002FD1}, {"LonesomeRoad.esm", 0x003605, 0x003606},
        {"GunRunnersArsenal.esm", 0x00093E, 0x00093D}};
    constexpr uint32_t kPackPopups[] = {0x00174777, 0x00174776, 0x001674D2, 0x00174775};
    constexpr uint32_t kPackScripts[] = {0x00172A27, 0x00172A24, 0x00172A26, 0x00172A25};
    constexpr uint32_t kCaps = 0x0000000F, kLockpick = 0x0000000A,
        k9mmPistol = 0x000E3982, k9mmAmmo = 0x00096BDF, kLaserPistol = 0x000301D3, kEnergyCells = 0x00029377,
        kDynamite = 0x000E39AD, kStraightRazor = 0x000F52B5, kBoxingGloves = 0x00107234;
    constexpr uint32_t kPlayerRef = 0x00000014;
    constexpr uint32_t kPlayerBase = 0x00000007;
    constexpr uint32_t kStandInBase = 0x00120EB1;
    constexpr uint32_t kIntroQuest = 0x00104C1C;
    constexpr uint32_t kPlayerFaction = 0x0001B2A4;
    constexpr uint32_t kDocMitchellHouse = 0x00103DF9;
    constexpr uint32_t kLvlPowderGanger = 0x0015F315;
    constexpr uint32_t kStimpak = 0x00015169;
    constexpr uint32_t kSuperStimpak = 0x000CCEF2;
    constexpr uint32_t k9mmPistolWeapon = 0x000E3778;
    constexpr uint32_t kGameYear = 0x00000035;
    constexpr uint32_t kGameMonth = 0x00000036;
    constexpr uint32_t kGameDay = 0x00000037;
    constexpr uint32_t kGameHour = 0x00000038;
    constexpr uint32_t kGameDaysPassed = 0x00000039;
}

namespace game::layout
{
    constexpr uint8_t kExtraLeveledCreature = 0x2E;
    constexpr uint32_t kLeveledBaseForm = 0x0C;
    constexpr uint32_t kLeveledActorBase = 0x10;
    constexpr uint8_t kExtraLock = 0x2A;
    constexpr uint8_t kExtraTeleport = 0x2B;
    constexpr uint8_t kExtraMerchantContainer = 0x3C;
    constexpr uint32_t kMerchantContainerRef = 0x0C;
    constexpr uint32_t kLockData = 0x0C;
    constexpr uint32_t kLockLevel = 0x00;
    constexpr uint32_t kLockFlags = 0x08;
    constexpr uint8_t kLockLocked = 0x01;
    constexpr uintptr_t kTesSingleton = 0x011DEA10;
    constexpr uint32_t kTesGridCells = 0x08;
    constexpr uint32_t kGridSize = 0x0C;
    constexpr uint32_t kGridCellArray = 0x10;
    constexpr uint32_t kCellWorldSpace = 0xC0;
    constexpr uint32_t kWorldPersistentCell = 0x34;
    constexpr uint8_t kFormTypeArmor = 0x18;
    constexpr uint8_t kFormTypeAlchemy = 0x2F;
    constexpr uint8_t kFormTypeLevItem = 0x34;
    constexpr uint32_t kArmorBipedFlags = 0x78;
    constexpr uint32_t kArmorMedium = 0x08;
    constexpr uint32_t kArmorHeavy = 0x80;
    constexpr uint32_t kArmorRating = 0x178;
    constexpr uint32_t kAlchFlags = 0xBC;
    constexpr uint8_t kAlchFood = 0x02;
    constexpr uint8_t kAlchMedicine = 0x04;
    constexpr uint32_t kLevListEntries = 0x34;
    constexpr uint32_t kLevListChanceNone = 0x3C;
    constexpr uint8_t kFormTypeAmmo = 0x29;
    constexpr uint32_t kAmmoValue = 0x7C;
    constexpr uint32_t kLevEntryCount = 0x04;
    constexpr uint32_t kLevListGlobal = 0x40;
    constexpr uint8_t kFormTypeContainer = 0x1B;
    constexpr uint8_t kFormTypeNpc = 0x2A;
    constexpr uint8_t kFormTypeCreature = 0x2B;
    constexpr uint32_t kContainerItemsOfBox = 0x34;
    constexpr uint32_t kContainerItemsOfActor = 0x68;
    constexpr uint8_t kFormTypeCombatStyle = 0x4A;
    constexpr uint32_t kStyleCoverRadius = 0x18;
    constexpr uint32_t kStyleTakeCover = 0x1C;
    constexpr uint32_t kStyleSideStep = 0x59;
    constexpr uint32_t kStyleFlags = 0xA8;
    constexpr uint16_t kStyleFleeWhenHurt = 0x04;
    constexpr uint32_t kPackageTarget = 0x30;
    constexpr uint32_t kIdleChildren = 0x40;
    constexpr uint32_t kNodeWorldRotate = 0x68;
    constexpr uint32_t kNodeWorldTranslate = 0x8C;
    constexpr uint32_t kRefPersistent = 0x400;
    constexpr uint32_t kRefDeleted = 0x20;
    constexpr uint32_t kRefDisabled = 0x800;
    constexpr uint8_t kExtraEnableStateParent = 0x37;
    constexpr uint32_t kEnableParentRef = 0x0C;
    constexpr uint32_t kProjFlags = 0xC8;
    constexpr uint32_t kProjTurnedOff = 0x200;
    constexpr uint16_t kMineBaseMask = 0x426, kMineBaseFlags = 0x26;
    constexpr uint32_t kContainerMenuRef = 0x74;
    constexpr uint32_t kBarterMenuMerchant = 0x80;
    constexpr uint32_t kInterfaceMenuRoot = 0x9C;
    constexpr uint32_t kTileFirstChild = 0x04, kTileName = 0x20, kTileNode = 0x2C;
    constexpr uint32_t kChildNodeNext = 0x00, kChildNodeTile = 0x08;
    constexpr uint32_t kNiLocalTranslate = 0x58, kNiLocalScale = 0x64;
    constexpr uint32_t kNiNodeChildren = 0xA0, kNiNodeChildSlots = 0xA6;
    constexpr uint32_t kDialogMenuPartner = 0x80;
    constexpr uint32_t kDialogMenuTopicNode = 0x70;
    constexpr uint32_t kMenuTopicResponses = 0x0C;
    constexpr uint32_t kMenuTopicInfo = 0x18;
    constexpr uint32_t kMenuTopicResponseNode = 0x20;
    constexpr uint32_t kMenuTopicTopic = 0x1C;
    constexpr uint32_t kResponseNumber = 0x28;
    constexpr uint32_t kIsTalking = 0x7D;
    constexpr uint32_t kDialogueItemInfo = 0x0C;
    constexpr uint32_t kActorProcess = 0x68;
    constexpr uint32_t kHudSubtitles = 0x198;
    constexpr uint32_t kHudSubtitleShown = 0x1A8;
    constexpr uint32_t kSubtitleShowFor = 0x108;
    constexpr uint8_t kExtraCount = 0x24;
    constexpr uint8_t kExtraDroppedItems = 0x3A;
    constexpr uint32_t kDroppedItemsList = 0x0C;
    constexpr uint32_t kCountValue = 0x0C;
    constexpr uint8_t kExtraWeaponModFlags = 0x8D;
    constexpr uint32_t kWeaponModFlagsValue = 0x0C;
    constexpr uint32_t kExtraWeaponModFlagsSize = 0x10;
    constexpr uintptr_t kVtblExtraWeaponModFlags = 0x010159A4;
    constexpr uint32_t kWeaponMods = 0x350;
    constexpr uint32_t kDataHandlerWorldSpaces = 0x10;
    constexpr uint32_t kWorldSpaceCell = 0x34;
    constexpr uint32_t kCellObjects = 0xAC;
    constexpr uint32_t kPlayerDisabledControls = 0x680;
    constexpr uint8_t kExtraMapMarker = 0x2C;
    constexpr uint32_t kMapMarkerData = 0x0C;
    constexpr uint32_t kMarkerName = 0x04;
    constexpr uint32_t kMarkerFlags = 0x0C;
    constexpr uint8_t kMarkerCanTravel = 2;
    constexpr uint32_t kDataHandlerQuests = 0x118;
    constexpr uint32_t kQuestFlags = 0x3C;
    constexpr uint32_t kQuestDelay = 0x40;
    constexpr uint32_t kQuestStages = 0x44;
    constexpr uint32_t kQuestObjectives = 0x4C;
    constexpr uint32_t kQuestCurrentStage = 0x60;
    constexpr uint32_t kQuestEditorId = 0x64;
    constexpr uint32_t kStageId = 0x00;
    constexpr uint32_t kStageDone = 0x01;
    constexpr uint32_t kObjectiveId = 0x04;
    constexpr uint32_t kObjectiveStatus = 0x20;
    constexpr uint32_t kPlayerActiveQuest = 0x6B8;
    constexpr uint32_t kPlayerObjectives = 0x6BC;
    constexpr uint32_t kPlayerTargets = 0x6C4;
    constexpr uint32_t kQuestScript = 0x1C;
    constexpr uint32_t kDataHandlerReputations = 0x90;
    constexpr uint32_t kDataHandlerModCount = 0x218;
    constexpr uint32_t kDataHandlerMods = 0x21C;
    constexpr uint32_t kModFileName = 0x20;
    constexpr uint32_t kReputationName = 0x1C;
    constexpr uint32_t kReputationFame = 0x44;
    constexpr uint32_t kReputationInfamy = 0x48;
    constexpr uint32_t kQuestName = 0x34;
    constexpr uint32_t kObjectiveText = 0x08;
    constexpr uint32_t kQuestLocals = 0x5C;
    constexpr uint32_t kLocalsVars = 0x0C;
    constexpr uint32_t kVarId = 0x00;
    constexpr uint32_t kVarValue = 0x08;
    constexpr uint8_t kQuestCompleted = 0x02;
    constexpr uint32_t kActorBaseEssential = 1u << 1;
    constexpr uint32_t kLifeStateDying = 1;
    constexpr uint32_t kLifeStateDead = 2;
    constexpr uint32_t kLifeStateUnconscious = 3;
    constexpr uint32_t kLifeStateEssentialDown = 6;
    constexpr int kControlActivate = 5;
    constexpr int kControlAttack = 4, kControlAimBlock = 6, kControlReadyItem = 7, kControlRun = 9,
        kControlAlwaysRun = 0x0A, kControlJump = 0x0C, kControlVats = 0x10, kControlGrab = 0x1B;
    constexpr uint32_t kPlayerAlwaysRun = 0x651;
}

namespace game::steamslots
{
    constexpr int kClientUser = 5, kClientFriends = 8, kClientNetworking = 17;
    constexpr const char* kUserVersion = "SteamUser013";
    constexpr int kUserSteamId = 2;
    constexpr const char* kVoiceUserVersion = "SteamUser021";
    constexpr int kVoiceStart = 7, kVoiceStop = 8, kVoiceAvailable = 9, kVoiceGet = 10, kVoiceDecompress = 11;
    constexpr const char* kNetworkingVersion = "SteamNetworking006";
    constexpr int kSend = 0, kAvailable = 1, kRead = 2, kAccept = 3, kClose = 4, kAllowRelay = 7;
    constexpr const char* kFriendsVersion = "SteamFriends018";
    constexpr int kPersonaName = 0, kFriendCount = 2, kFriendByIndex = 3, kRelationship = 4, kFriendName = 6,
        kFriendGame = 7, kSetPresence = 41, kClearPresence = 42, kFriendPresence = 43;
    constexpr int kSessionRequest = 1202, kConnectFail = 1203;
    constexpr uint32_t kNewVegasAppId = 22380;
}
