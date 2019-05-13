#include "InGameMode.h"
#include "ItemSpawner.h"
#include "InGameWidget.h"
#include "RedZoneSpawner.h"
#include "LobbyWidget.h"
#include "HANSEIRacingController.h"
#include "HANSEIRacingGameInstance.h"
#include "DefaultVehicleCharacter.h"
#include "GameFramework/PlayerStart.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundCue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "ConstructorHelpers.h"
#include <algorithm>

AInGameMode::AInGameMode() : m_InGameWidgetClass(nullptr), m_InGameWidget(nullptr), m_LobbyWidgetClass(nullptr), m_LobbyWidget(nullptr), m_GameInstance(nullptr), m_Character(nullptr), m_LobbySoundComponent(nullptr), m_InGameSoundComponent(nullptr), m_bIsHaveToSpawnPlayer(false), m_bIsLeader(false), m_bIsReady(false), m_bIsInGame(false), m_bChangeGameSetting(true), m_bIsSucceedStartGame(false), m_SocketNumber(0), m_LobbyCamera(nullptr), m_RedZoneManager(nullptr) {
	ConstructorHelpers::FClassFinder<ADefaultVehicleCharacter> VehicleClass(L"Blueprint'/Game/BluePrint/Vehicle/BP_DefaultVehicleCharacter.BP_DefaultVehicleCharacter_C'");

	ConstructorHelpers::FObjectFinder<USoundCue> LobbyBG(L"SoundCue'/Game/Sound/InGameTheme/LobbyTheme/LobbyTheme_Cue.LobbyTheme_Cue'");
	ConstructorHelpers::FObjectFinder<USoundCue> LobbyBG1(L"SoundCue'/Game/Sound/InGameTheme/LobbyTheme/LobbyTheme2_Cue.LobbyTheme2_Cue'");
	ConstructorHelpers::FObjectFinder<USoundCue> LobbyBG2(L"SoundCue'/Game/Sound/InGameTheme/LobbyTheme/LobbyTheme3_Cue.LobbyTheme3_Cue'");
	ConstructorHelpers::FObjectFinder<USoundCue> LobbyBG3(L"SoundCue'/Game/Sound/InGameTheme/LobbyTheme/LobbyTheme4_Cue.LobbyTheme4_Cue'");

	ConstructorHelpers::FObjectFinder<USoundCue> InGameBG(L"SoundCue'/Game/Sound/InGameTheme/GameMusic/InGame_Theme_Cue.InGame_Theme_Cue'");
	ConstructorHelpers::FObjectFinder<USoundCue> InGameBG1(L"SoundCue'/Game/Sound/InGameTheme/GameMusic/InGame_2_Theme_Cue.InGame_2_Theme_Cue'");
	ConstructorHelpers::FObjectFinder<USoundCue> InGameBG2(L"SoundCue'/Game/Sound/InGameTheme/GameMusic/InGame_3_Theme_Cue.InGame_3_Theme_Cue'");

	ConstructorHelpers::FClassFinder<ULobbyWidget> LobbyWidget(L"WidgetBlueprint'/Game/UI/BP_LobbyWidget.BP_LobbyWidget_C'");
	ConstructorHelpers::FClassFinder<UInGameWidget> InGameWidget(L"WidgetBlueprint'/Game/UI/BP_InGameUI.BP_InGameUI_C'");

	if (LobbyBG.Succeeded() && LobbyBG1.Succeeded() && LobbyBG2.Succeeded() && LobbyBG3.Succeeded()) {
		m_LobbySoundCues.Add(LobbyBG.Object);
		m_LobbySoundCues.Add(LobbyBG1.Object);
		m_LobbySoundCues.Add(LobbyBG2.Object);
		m_LobbySoundCues.Add(LobbyBG3.Object);
	}
	if (InGameBG.Succeeded() && InGameBG1.Succeeded() && InGameBG2.Succeeded()) {
		m_InGameSoundCues.Add(InGameBG.Object);
		m_InGameSoundCues.Add(InGameBG1.Object);
		m_InGameSoundCues.Add(InGameBG2.Object);
	}
	if (VehicleClass.Succeeded()) {
		m_VehicleClass = VehicleClass.Class;
	}
	if (LobbyWidget.Succeeded()) {
		m_LobbyWidgetClass = LobbyWidget.Class;
	}
	if (InGameWidget.Succeeded()) {
		m_InGameWidgetClass = InGameWidget.Class;
	}

	PlayerControllerClass = AHANSEIRacingController::StaticClass();

	m_Port = 3500;
	m_SocketName = L"Login_Socket";

	PrimaryActorTick.bCanEverTick = true;
}

void AInGameMode::BeginPlay() {
	ABaseGameMode::BeginPlay();

	if (m_LobbyWidgetClass) {
		m_LobbyWidget = CreateWidget<ULobbyWidget>(GetWorld(), m_LobbyWidgetClass);
		if (IsValidLowLevelFast(m_LobbyWidget)) {
			m_LobbyWidget->AddToViewport();
		}
	}
	if (m_InGameWidgetClass) {
		m_InGameWidget = CreateWidget<UInGameWidget>(GetWorld(), m_InGameWidgetClass);
		if (IsValidLowLevelFast(m_InGameWidget)) {
			m_InGameWidget->AddToViewport();
			m_InGameWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), m_SpawnPoint);
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AItemSpawner::StaticClass(), m_ItemSpawners);

	m_GameInstance = Cast<UHANSEIRacingGameInstance>(GetGameInstance());
	if(!m_GameInstance){
		m_GameInstance = Cast<UHANSEIRacingGameInstance>(GetGameInstance());
	}
	SendJoinGameToServer();
}

void AInGameMode::Tick(float DeltaTime) {
	ABaseGameMode::Tick(DeltaTime);

	if (IsInGameThread()) {
		if (m_bIsInGame) {
			if (m_bIsSucceedStartGame && IsValid(m_InGameWidget)) {
				m_InGameWidget->SetCountdownTimer(5, L"START!", [this]() {
					auto Controller = GetWorld()->GetFirstPlayerController();
					if (IsValid(Controller)) {
						FInputModeGameOnly GameInput;
						Controller->SetInputMode(GameInput);
					}
				});
				m_bIsSucceedStartGame = false;
			}
			UpdatePlayerLocationAndRotation();
		}

		if (m_bIsHaveToSpawnPlayer) {
			SpawnCharacter();
		}
		if (m_bChangeGameSetting) {
			ChangeGameSetting();
		}
	}
}

void AInGameMode::BeginDestroy() {
	SendDisconnectToServer();

	ABaseGameMode::BeginDestroy();
}

void AInGameMode::RecvDataProcessing(uint8* RecvBuffer, int32& RecvBytes) {
	PACKET* Packet = reinterpret_cast<PACKET*>(RecvBuffer);
	if (Packet) {
		int32 PacketSize = GetPacketSize(Packet);
		if (PacketSize > 0) {
			m_PacketQueue.push(Packet);
			for (int32 i = 1; i < (RecvBytes / PacketSize); i++) {
				uint8* ShiftBuffer = RecvBufferShiftProcess(RecvBuffer, PacketSize, i);
				PACKET* Pack = reinterpret_cast<PACKET*>(ShiftBuffer);
				if (Pack) {
					m_PacketQueue.push(Pack);
					PacketSize = GetPacketSize(m_PacketQueue.front());
					if (PacketSize <= 0) {
						break;
					}
				}
			}
		}
	}
	
	while (!m_PacketQueue.empty()) {
		PACKET* Packet = m_PacketQueue.front();
		
		if (Packet) {
			switch (Packet->m_MessageType) {
			case EPACKETMESSAGEFORGAMETYPE::EPMGT_JOIN:
				IsSucceedJoinGame(*static_cast<GAMEPACKET*>(Packet));
				break;
			case EPACKETMESSAGEFORGAMETYPE::EPMGT_NEWPLAYER:
				IsSucceedJoinGameNewPlayer(*static_cast<GAMEPACKET*>(Packet));
				break;
			case EPACKETMESSAGEFORGAMETYPE::EPMGT_DISCONNECTOTHER:
				IsSucceedDisconnectOtherPlayer(*static_cast<GAMEPACKET*>(Packet));
				break;
			case EPACKETMESSAGEFORGAMETYPE::EPMGT_UPDATE:
				IsSucceedUpdatePlayerInformation(*static_cast<GAMEPACKET*>(Packet));
				break;
			case EPACKETMESSAGEFORGAMETYPE::EPMGT_POSSESS:
				IsSucceedPossessingVehicle(*static_cast<GAMEPACKET*>(Packet));
				break;
			case EPACKETMESSAGEFORGAMETYPE::EPMGT_START:
				IsSucceedStartGame(*static_cast<GAMEPACKET*>(Packet));
				break;
			case EPACKETMESSAGEFORGAMETYPE::EPMGT_READY:
				IsSucceedChangeReadyState(*static_cast<GAMEPACKET*>(Packet));
				break;
			case EPACKETMESSAGEFORGAMETYPE::EPMGT_RESPAWNPLAYER:
				IsSucceedRespawnPlayer(*static_cast<GAMEPACKET*>(Packet));
				break;
			case EPACKETMESSAGEFORGAMETYPE::EPMGT_SPAWNITEM:
				IsSucceedRespawnItem(*static_cast<SPAWNERPACKET*>(Packet));
				break;
			case EPACKETMESSAGEFORGAMETYPE::EPMGT_REDZONESTART:
				IsSucceedStartRedZone(*static_cast<REDZONEPACKET*>(Packet));
				break;
			}
		}
		m_PacketQueue.pop();
	}
}

// FROM Server

void AInGameMode::IsSucceedJoinGame(GAMEPACKET& Packet) {
	if (Packet.m_FailedReason == EPACKETFAILEDTYPE::EPFT_SUCCEED) {
		if (m_GameInstance && m_GameInstance->IsValidLowLevel()) {
			m_GameInstance->SetUniqueKey(Packet.m_UniqueKey);
		}
		m_bIsLeader = Packet.m_bIsLeader;
		m_SocketNumber = Packet.m_Socket;
		m_PlayerList.push_back(Packet);
		m_bIsHaveToSpawnPlayer = true;
		RefreshLobbyWidgetData();
	}
}

void AInGameMode::IsSucceedJoinGameNewPlayer(GAMEPACKET& Packet) {
	if (Packet.m_FailedReason == EPACKETFAILEDTYPE::EPFT_SUCCEED) {
		m_PlayerList.push_back(Packet);
		m_bIsHaveToSpawnPlayer = true;
		RefreshLobbyWidgetData();
	}
}

void AInGameMode::IsSucceedDisconnectOtherPlayer(GAMEPACKET& Packet) {
	if (Packet.m_FailedReason == EPACKETFAILEDTYPE::EPFT_SUCCEED) {
		auto Character = m_CharacterClass.Find(Packet.m_UniqueKey);
		auto CharacterList = std::find_if(m_PlayerList.begin(), m_PlayerList.end(), [&](const GAMEPACKET& Data) -> bool { if (Data.m_UniqueKey == Packet.m_UniqueKey) { return true; } return false; });
		if (Character != nullptr && CharacterList != m_PlayerList.cend()) {
			(*Character)->SetIsDisconnect(true);
			m_PlayerList.erase(CharacterList);
			m_CharacterClass.Remove(Packet.m_UniqueKey);
		}

		if (m_bIsInGame) {
			RefreshInGameWidgetData();
		}
		else{
			RefreshLobbyWidgetData();
		}
	}
}

void AInGameMode::IsSucceedUpdatePlayerInformation(GAMEPACKET& Packet) {
	if (Packet.m_FailedReason == EPACKETFAILEDTYPE::EPFT_SUCCEED) {
		GEngine->AddOnScreenDebugMessage(2, 1.f, FColor::Red, FString::Printf(L"Recv Time : %f", GetWorld()->GetRealTimeSeconds()));

		auto Character = std::find_if(m_PlayerList.begin(), m_PlayerList.end(), [&](const GAMEPACKET& Data) -> bool { if (Data.m_UniqueKey == Packet.m_UniqueKey) { return true; } return false; });
		if (Character != m_PlayerList.cend()) {
			(*Character) = Packet;
		}
	}
}

void AInGameMode::IsSucceedPossessingVehicle(GAMEPACKET& Packet) {
	if (Packet.m_FailedReason == EPACKETFAILEDTYPE::EPFT_SUCCEED) {
		m_bChangeGameSetting = m_bIsInGame = true;
	}
}

void AInGameMode::IsSucceedStartGame(GAMEPACKET& Packet) {
	if (Packet.m_FailedReason == EPACKETFAILEDTYPE::EPFT_SUCCEED) {
		m_bIsSucceedStartGame = true;
	}
}

void AInGameMode::IsSucceedRespawnPlayer(GAMEPACKET & Packet) {
	if (Packet.m_FailedReason == EPACKETFAILEDTYPE::EPFT_SUCCEED) {
		auto Character = std::find_if(m_PlayerList.begin(), m_PlayerList.end(), [&](const GAMEPACKET& Data) -> bool { if (Data.m_UniqueKey == Packet.m_UniqueKey) { return true; } return false; });
		if (Character != m_PlayerList.cend()) {
			(*Character) = Packet;
		}
		m_bIsHaveToSpawnPlayer = true;
	}
}

void AInGameMode::IsSucceedChangeReadyState(GAMEPACKET& Packet) {
	if (!m_bIsInGame) {
		if (Packet.m_FailedReason == EPACKETFAILEDTYPE::EPFT_SUCCEED) {
			auto Character = std::find_if(m_PlayerList.begin(), m_PlayerList.end(), [&](const GAMEPACKET& Data) -> bool { if (Data.m_UniqueKey == Packet.m_UniqueKey) { return true; } return false; });
			if (Character != m_PlayerList.cend()) {
				if (Character->m_UniqueKey == m_GameInstance->GetUniqueKey()) {
					m_bIsReady = Packet.m_bIsReady;
				}
				Character->m_bIsReady = Packet.m_bIsReady;
				RefreshLobbyWidgetData();
			}
		}
	}
}

void AInGameMode::IsSucceedRespawnItem(SPAWNERPACKET& Packet) {
	if (Packet.m_FailedReason == EPACKETFAILEDTYPE::EPFT_SUCCEED) {
		if (Packet.m_ItemInformation.m_SpawnerID >= 0 && Packet.m_ItemInformation.m_SpawnerID < m_ItemSpawners.Num()) {
			auto SpawnerActor = Cast<AItemSpawner>(*m_ItemSpawners.FindByPredicate([&](AActor* Actor) -> bool { 
				if (IsValid(Actor) && Actor->IsA<AItemSpawner>()) {
					if (Cast<AItemSpawner>(Actor)->GetSpawnerUniqueID() == Packet.m_ItemInformation.m_SpawnerID) {
						return true;
					}
				}
				return false;
			}));
			if (SpawnerActor && IsValid(SpawnerActor)) {
				SpawnerActor->FindItemIndexAndReset(Packet.m_ItemInformation.m_Index);
			}
		}
	}
}

void AInGameMode::IsSucceedStartRedZone(REDZONEPACKET& Packet) {
	if (Packet.m_FailedReason == EPACKETFAILEDTYPE::EPFT_SUCCEED && IsValid(m_RedZoneManager)) {
		m_RedZoneManager->SetActivateRedZone(true, Packet.m_SplinePoint);
	}
}

// TO Server

void AInGameMode::SendCanStartToServer() {
	GAMEPACKET Packet;
	memset(&Packet, 0, sizeof(Packet));

	if (GetSocket() && m_GameInstance && m_GameInstance->IsValidLowLevel()) {
		Packet.m_PacketType = EPACKETTYPE::EPT_PLAYER;
		Packet.m_MessageType = EPACKETMESSAGEFORGAMETYPE::EPMGT_START;
		Packet.m_SessionID = m_GameInstance->GetSessionID();
		Packet.m_Socket = m_SocketNumber;

		GetSocket()->Send((char*)&Packet, sizeof(GAMEPACKET));
	}
}

void AInGameMode::SendJoinGameToServer() {
	GAMEPACKET Packet;
	memset(&Packet, 0, sizeof(Packet));

	if (GetSocket() && m_GameInstance && m_GameInstance->IsValidLowLevel()) {
		Packet.m_PacketType = EPACKETTYPE::EPT_PLAYER;
		Packet.m_MessageType = EPACKETMESSAGEFORGAMETYPE::EPMGT_JOIN;
		Packet.m_SessionID = m_GameInstance->GetSessionID();
		Packet.m_Socket = m_SocketNumber;
		strncpy(Packet.m_PlayerNickName, TCHAR_TO_ANSI(*m_GameInstance->GetPlayerNickName()), MaxNameLen);

		GetSocket()->Send((char*)&Packet, sizeof(GAMEPACKET));
	}
}

void AInGameMode::SendCharacterInformationToServer(const FVector& Location, const FRotator& Rotation, const FInputMotionData& Data, const float& Health) {
	GAMEPACKET Packet;
	RANK PlayerRankInformation;
	auto Controller = Cast<AHANSEIRacingController>(IsValid(m_Character) ? m_Character->GetController() : nullptr);
	memset(&Packet, 0, sizeof(Packet));

	if (GetSocket() && IsValid(Controller) && m_GameInstance && m_GameInstance->IsValidLowLevel()) {
		PlayerRankInformation.m_CurrentSplinePoint = Controller->GetCurrentSplinePoint();
		PlayerRankInformation.m_SplinePointDistance = Controller->GetSplinePointDistance();
		
		Packet.m_RankInformation = PlayerRankInformation;
		Packet.m_PacketType = EPACKETTYPE::EPT_PLAYER;
		Packet.m_MessageType = EPACKETMESSAGEFORGAMETYPE::EPMGT_UPDATE;
		Packet.m_SessionID = m_GameInstance->GetSessionID();
		Packet.m_UniqueKey = m_GameInstance->GetUniqueKey();
		Packet.m_Socket = m_SocketNumber;
		Packet.m_Location = Location;
		Packet.m_Rotation = Rotation;
		Packet.m_VehicleData = Data;
		Packet.m_Health = Health;

		GetSocket()->Send((char*)&Packet, sizeof(GAMEPACKET));

		m_LastSendTime = GetWorld()->GetRealTimeSeconds();
		GEngine->AddOnScreenDebugMessage(1, 1.f, FColor::Red, FString::Printf(L"Send Time : %f", m_LastSendTime));
	}
}

void AInGameMode::SendDisconnectToServer() {
	GAMEPACKET Packet;
	memset(&Packet, 0, sizeof(Packet));

	if (GetSocket() && m_GameInstance && m_GameInstance->IsValidLowLevel()) {
		Packet.m_PacketType = EPACKETTYPE::EPT_PLAYER;
		Packet.m_MessageType = EPACKETMESSAGEFORGAMETYPE::EPMGT_DISCONNECT;
		Packet.m_SessionID = m_GameInstance->GetSessionID();
		Packet.m_UniqueKey = m_GameInstance->GetUniqueKey();
		Packet.m_Socket = m_SocketNumber;

		GetSocket()->Send((char*)&Packet, sizeof(GAMEPACKET));
	}
}

void AInGameMode::SendPossessTheVehicleToServer() {
	if (!m_bIsInGame) {
		GAMEPACKET Packet;
		memset(&Packet, 0, sizeof(Packet));

		if (GetSocket() && IsValid(m_GameInstance)) {
			Packet.m_PacketType = EPACKETTYPE::EPT_PLAYER;
			Packet.m_MessageType = EPACKETMESSAGEFORGAMETYPE::EPMGT_POSSESS;
			Packet.m_SessionID = m_GameInstance->GetSessionID();
			Packet.m_Socket = m_SocketNumber;

			GetSocket()->Send((ANSICHAR*)&Packet, sizeof(GAMEPACKET));
		}
	}
}

void AInGameMode::SendChangeReadyStateToServer() {
	if (!m_bIsInGame) {
		GAMEPACKET Packet;
		memset(&Packet, 0, sizeof(Packet));

		if (GetSocket() && IsValid(m_GameInstance)) {
			Packet.m_PacketType = EPACKETTYPE::EPT_PLAYER;
			Packet.m_MessageType = EPACKETMESSAGEFORGAMETYPE::EPMGT_READY;
			Packet.m_SessionID = m_GameInstance->GetSessionID();
			Packet.m_UniqueKey = m_GameInstance->GetUniqueKey();
			Packet.m_bIsReady = !m_bIsReady;
			Packet.m_Socket = m_SocketNumber;

			GetSocket()->Send((ANSICHAR*)&Packet, sizeof(GAMEPACKET));
		}
	}
}

void AInGameMode::SendRespawnItemToServer(const int32 & SpawnerID, const int32 & ItemIndex) {
	GAMEPACKET Packet;
	memset(&Packet, 0, sizeof(Packet));

	if (GetSocket() && IsValid(m_GameInstance)) {
		Packet.m_PacketType = EPACKETTYPE::EPT_SPAWNER;
		Packet.m_MessageType = EPACKETMESSAGEFORGAMETYPE::EPMGT_SPAWNITEM;
		Packet.m_SessionID = m_GameInstance->GetSessionID();
		Packet.m_ItemInformation.m_bIsActivated = true;
		Packet.m_ItemInformation.m_SpawnerID = SpawnerID;
		Packet.m_ItemInformation.m_Index = ItemIndex;
		Packet.m_Socket = m_SocketNumber;

		GetSocket()->Send((ANSICHAR*)&Packet, sizeof(GAMEPACKET));
	}
}

// Private Function

void AInGameMode::SpawnCharacter() {
	AHANSEIRacingController* Controller = Cast<AHANSEIRacingController>(GetWorld()->GetFirstPlayerController());

	if (m_GameInstance && m_GameInstance->IsValidLowLevel() && m_SpawnPoint.Num() >= m_PlayerList.size() && IsValid(Controller)) {
		for (int32 i = 0; i < m_PlayerList.size(); i++) {
			if (!m_CharacterClass.Find(m_PlayerList[i].m_UniqueKey)) {
				AActor** SpawnPoint = FindSpawnPointByUniqueKey(m_PlayerList[i].m_UniqueKey);

				if (SpawnPoint && IsValid(*SpawnPoint)) {
					FVector Location = (*SpawnPoint)->GetActorLocation();
					FRotator Rotation = (*SpawnPoint)->GetActorRotation();
					FActorSpawnParameters Param;
					Param.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					auto NewPawn = GetWorld()->SpawnActor<ADefaultVehicleCharacter>(m_VehicleClass, Location, Rotation, Param);

					if (NewPawn) {
						SpawnPawnAndAddCharacterList(NewPawn, m_PlayerList[i].m_UniqueKey, m_PlayerList[i].m_PlayerNickName, m_PlayerList[i].m_RankInformation.m_CurrentRank);
						if (m_CharacterClass.Num() == m_PlayerList.size()) {
							RefreshInGameWidgetData();
							m_bIsHaveToSpawnPlayer = false;
						}
					}
				}
			}
		}
	}
}

void AInGameMode::ChangeGameSetting() {
	if (m_bChangeGameSetting) {
		if (ChangePossessState() && ChangeWidgetVisibility() && ChangeBackGroundSound()) {
			m_bChangeGameSetting = false;
		}
	}
}

void AInGameMode::UpdatePlayerLocationAndRotation() {
	if (m_GameInstance && m_GameInstance->IsValidLowLevel()) {
		bool bIsUpdatedRank = false;

		for (auto It : m_PlayerList) {
			auto Character = m_CharacterClass.Find(It.m_UniqueKey);
			if (Character && IsValid(*Character)) {
				if (It.m_UniqueKey != m_GameInstance->GetUniqueKey()) {
					FVector Location(It.m_Location.x, It.m_Location.y, It.m_Location.z);
					FRotator Rotation(It.m_Rotation.x, It.m_Rotation.y, It.m_Rotation.z);

					(*Character)->SetActorLocationAndRotation(Location, FQuat(Rotation), false, nullptr, ETeleportType::TeleportPhysics);
					(*Character)->SetVehicleState(It.m_VehicleData);
				}

				if ((*Character)->GetPlayerCurrentRank() != It.m_RankInformation.m_CurrentRank) {
					(*Character)->SetPlayerRank(It.m_RankInformation.m_CurrentRank);
					bIsUpdatedRank = true;
				}

				if (It.m_bIsDead) {
					m_CharacterClass.Remove(It.m_UniqueKey);
					(*Character)->SetIsDead(true);
				}
			}
		}

		if (bIsUpdatedRank) {
			RefreshInGameWidgetData();
		}
	}
}

// Inline Private Function

uint8* AInGameMode::RecvBufferShiftProcess(uint8* RecvBuffer, const int32& PacketSize, const int32& CurrentCount) {
	return RecvBuffer + (PacketSize * CurrentCount);
}

int32 AInGameMode::GetPacketSize(const PACKET* Packet) {
	switch (Packet->m_PacketType) {
	case EPACKETTYPE::EPT_PLAYER:
		return sizeof(GAMEPACKET);
	case EPACKETTYPE::EPT_SPAWNER:
		return sizeof(SPAWNERPACKET);
	case EPACKETTYPE::EPT_REDZONE:
		return sizeof(REDZONEPACKET);
	}
	return 0;
}

AActor** AInGameMode::FindSpawnPointByUniqueKey(const int32& UniqueKey) {
	auto SpawnPoint = m_SpawnPoint.FindByPredicate([this, &UniqueKey](const AActor* Actor) {
		auto PlayerStart = Cast<APlayerStart>(Actor);
		if (IsValid(PlayerStart) && PlayerStart->PlayerStartTag.Compare(*FString::Printf(L"%d", (UniqueKey % m_SpawnPoint.Num()))) == 0) {
			return true;
		}
		return false;
	});
	return SpawnPoint;
}

void AInGameMode::SpawnPawnAndAddCharacterList(ADefaultVehicleCharacter* NewPawn, const int32& UniqueKey, const ANSICHAR* PlayerName, const int32& PlayerRank) {
	if (!IsValid(m_Character) && UniqueKey == m_GameInstance->GetUniqueKey()) {
		m_Character = NewPawn;
		NewPawn->SetIsItPlayer(true);

		if(m_bIsInGame){
			AHANSEIRacingController* Controller = Cast<AHANSEIRacingController>(GetWorld()->GetFirstPlayerController());
			Controller->Possess(NewPawn);
		}
	}
	else {
		NewPawn->SpawnDefaultController();
	}

	if (PlayerName) {
		NewPawn->SetPlayerName(FString(PlayerName));
	}
	NewPawn->SetMaterialFromUniqueKey(UniqueKey);
	NewPawn->SetPlayerRank(PlayerRank);
	m_CharacterClass.Add(TPairInitializer<int32, ADefaultVehicleCharacter*>(UniqueKey, NewPawn));
}

bool AInGameMode::ChangePossessState() {
	auto Controller = Cast<AHANSEIRacingController>(GetWorld()->GetFirstPlayerController());

	if (m_bIsInGame) {
		auto Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
		if (IsValid(m_Character) && IsValid(Pawn)) {
			if (Pawn != m_Character) {
				Controller->GetPawn()->Destroy();
			}
			Controller->Possess(m_Character);
			Controller->bShowMouseCursor = false;
			Controller->StartLocationSendTimer();
			SendCanStartToServer();

			return true;
		}
	}
	else {
		if (IsValid(m_LobbyCamera) && IsValid(Controller)) {
			FInputModeUIOnly UIInput;
			Controller->SetViewTarget(m_LobbyCamera);
			Controller->SetInputMode(UIInput);
			Controller->bShowMouseCursor = true;
			Controller->StopLocationSendTimer();

			return true;
		}
	}
	return false;
}

bool AInGameMode::ChangeWidgetVisibility() {
	if (IsValid(m_InGameWidget) && IsValid(m_LobbyWidget)) {
		m_LobbyWidget->SetVisibility(m_bIsInGame ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		m_InGameWidget->SetVisibility(m_bIsInGame ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

		return true;
	}
	return false;
}

bool AInGameMode::ChangeBackGroundSound() {
	if (m_bIsInGame) {
		if (IsValid(m_LobbySoundComponent)) {
			m_LobbySoundComponent->Stop();
		}
		m_InGameSoundComponent = UGameplayStatics::SpawnSound2D(GetWorld(), m_InGameSoundCues[FMath::RandRange(0, m_InGameSoundCues.Num() - 1)]);
	}
	else {
		if (IsValid(m_InGameSoundComponent)) {
			m_InGameSoundComponent->Stop();
		}
		m_LobbySoundComponent = UGameplayStatics::SpawnSound2D(GetWorld(), m_LobbySoundCues[FMath::RandRange(0, m_LobbySoundCues.Num() - 1)]);
	}
	return true;
}

void AInGameMode::RefreshLobbyWidgetData() {
	if (IsValidLowLevelFast(m_LobbyWidget)) {
		m_LobbyWidget->SetPlayerList(m_PlayerList);
	}
}

void AInGameMode::RefreshInGameWidgetData() {
	if (IsValidLowLevelFast(m_InGameWidget)) {
		m_InGameWidget->SetCharacterClassData(m_CharacterClass);
	}
}