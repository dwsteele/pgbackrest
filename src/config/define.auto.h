/***********************************************************************************************************************************
Command and Option Definition

Automatically generated by Build.pm -- do not modify directly.
***********************************************************************************************************************************/
#ifndef CONFIG_DEFINE_AUTO_H
#define CONFIG_DEFINE_AUTO_H

/***********************************************************************************************************************************
Command define enum
***********************************************************************************************************************************/
typedef enum
{
    cfgDefCmdArchiveGet,
    cfgDefCmdArchivePush,
    cfgDefCmdBackup,
    cfgDefCmdCheck,
    cfgDefCmdExpire,
    cfgDefCmdHelp,
    cfgDefCmdInfo,
    cfgDefCmdLocal,
    cfgDefCmdRemote,
    cfgDefCmdRestore,
    cfgDefCmdStanzaCreate,
    cfgDefCmdStanzaDelete,
    cfgDefCmdStanzaUpgrade,
    cfgDefCmdStart,
    cfgDefCmdStop,
    cfgDefCmdVersion,
} ConfigDefineCommand;

/***********************************************************************************************************************************
Option type define enum
***********************************************************************************************************************************/
typedef enum
{
    cfgDefOptTypeBoolean,
    cfgDefOptTypeFloat,
    cfgDefOptTypeHash,
    cfgDefOptTypeInteger,
    cfgDefOptTypeList,
    cfgDefOptTypeString,
} ConfigDefineOptionType;

/***********************************************************************************************************************************
Option define enum
***********************************************************************************************************************************/
typedef enum
{
    cfgDefOptArchiveAsync,
    cfgDefOptArchiveCheck,
    cfgDefOptArchiveCopy,
    cfgDefOptArchiveQueueMax,
    cfgDefOptArchiveTimeout,
    cfgDefOptBackupCmd,
    cfgDefOptBackupConfig,
    cfgDefOptBackupHost,
    cfgDefOptBackupSshPort,
    cfgDefOptBackupStandby,
    cfgDefOptBackupUser,
    cfgDefOptBufferSize,
    cfgDefOptChecksumPage,
    cfgDefOptCmdSsh,
    cfgDefOptCommand,
    cfgDefOptCompress,
    cfgDefOptCompressLevel,
    cfgDefOptCompressLevelNetwork,
    cfgDefOptConfig,
    cfgDefOptDbCmd,
    cfgDefOptDbConfig,
    cfgDefOptDbHost,
    cfgDefOptDbInclude,
    cfgDefOptDbPath,
    cfgDefOptDbPort,
    cfgDefOptDbSocketPath,
    cfgDefOptDbSshPort,
    cfgDefOptDbTimeout,
    cfgDefOptDbUser,
    cfgDefOptDelta,
    cfgDefOptForce,
    cfgDefOptHardlink,
    cfgDefOptHostId,
    cfgDefOptLinkAll,
    cfgDefOptLinkMap,
    cfgDefOptLockPath,
    cfgDefOptLogLevelConsole,
    cfgDefOptLogLevelFile,
    cfgDefOptLogLevelStderr,
    cfgDefOptLogPath,
    cfgDefOptLogTimestamp,
    cfgDefOptManifestSaveThreshold,
    cfgDefOptNeutralUmask,
    cfgDefOptOnline,
    cfgDefOptOutput,
    cfgDefOptPerlBin,
    cfgDefOptPerlOption,
    cfgDefOptProcess,
    cfgDefOptProcessMax,
    cfgDefOptProtocolTimeout,
    cfgDefOptRecoveryOption,
    cfgDefOptRepoCipherPass,
    cfgDefOptRepoCipherType,
    cfgDefOptRepoPath,
    cfgDefOptRepoS3Bucket,
    cfgDefOptRepoS3CaFile,
    cfgDefOptRepoS3CaPath,
    cfgDefOptRepoS3Endpoint,
    cfgDefOptRepoS3Host,
    cfgDefOptRepoS3Key,
    cfgDefOptRepoS3KeySecret,
    cfgDefOptRepoS3Region,
    cfgDefOptRepoS3VerifySsl,
    cfgDefOptRepoType,
    cfgDefOptResume,
    cfgDefOptRetentionArchive,
    cfgDefOptRetentionArchiveType,
    cfgDefOptRetentionDiff,
    cfgDefOptRetentionFull,
    cfgDefOptSet,
    cfgDefOptSpoolPath,
    cfgDefOptStanza,
    cfgDefOptStartFast,
    cfgDefOptStopAuto,
    cfgDefOptTablespaceMap,
    cfgDefOptTablespaceMapAll,
    cfgDefOptTarget,
    cfgDefOptTargetAction,
    cfgDefOptTargetExclusive,
    cfgDefOptTargetTimeline,
    cfgDefOptTest,
    cfgDefOptTestDelay,
    cfgDefOptTestPoint,
    cfgDefOptType,
} ConfigDefineOption;

#endif
