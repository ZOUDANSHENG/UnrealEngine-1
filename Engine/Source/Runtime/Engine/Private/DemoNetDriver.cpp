// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UDemoNetDriver.cpp: Simulated network driver for recording and playing back game sessions.
=============================================================================*/

#include "EnginePrivate.h"
#include "Engine/DemoNetDriver.h"
#include "Engine/DemoNetConnection.h"
#include "Engine/DemoPendingNetGame.h"
#include "Engine/ActorChannel.h"
#include "RepLayout.h"

DEFINE_LOG_CATEGORY_STATIC( LogDemo, Log, All );

static TAutoConsoleVariable<float> CVarDemoRecordHz( TEXT( "demo.RecordHz" ), 10, TEXT( "Number of demo frames recorded per second" ) );

static const int32 MAX_DEMO_READ_WRITE_BUFFER = 1024 * 2;

#define DEMO_CHECKSUMS 0

/*-----------------------------------------------------------------------------
	UDemoNetDriver.
-----------------------------------------------------------------------------*/

UDemoNetDriver::UDemoNetDriver(const class FPostConstructInitializeProperties& PCIP)
	: Super(PCIP)
{
}

bool UDemoNetDriver::InitBase( bool bInitAsClient, FNetworkNotify* InNotify, const FURL& URL, bool bReuseAddressAndPort, FString& Error )
{
	if ( Super::InitBase( bInitAsClient, InNotify, URL, bReuseAddressAndPort, Error ) )
	{
		DemoFilename			= URL.Map;
		Time					= 0;
		DemoFrameNum			= 0;
		bIsRecordingDemoFrame	= false;

		return true;
	}

	return false;
}

void UDemoNetDriver::FinishDestroy()
{
	if ( !HasAnyFlags( RF_ClassDefaultObject ) )
	{
		// Make sure we stop any recording that might be going on
		if ( ClientConnections.Num() > 0 )
		{
			StopDemo();
		}
	}

	Super::FinishDestroy();
}

FString UDemoNetDriver::LowLevelGetNetworkNumber()
{
	return FString( TEXT( "" ) );
}

bool UDemoNetDriver::InitConnect( FNetworkNotify* InNotify, const FURL& ConnectURL, FString& Error )
{
	// handle default initialization
	if ( !InitBase( true, InNotify, ConnectURL, false, Error ) )
	{
		return false;
	}

	// open the pre-recorded demo file
	FileAr = IFileManager::Get().CreateFileReader( *DemoFilename );

	if ( !FileAr )
	{
		Error = FString::Printf( TEXT( "Couldn't open demo file %s for reading" ), *DemoFilename );
		UE_LOG( LogDemo, Error, TEXT( "UDemoNetDriver::InitConnect: %s" ), *Error );
		return false;
	}

	// Playback, local machine is a client, and the demo stream acts "as if" it's the server.
	ServerConnection = ConstructObject<UNetConnection>( UDemoNetConnection::StaticClass() );
	ServerConnection->InitConnection( this, USOCK_Pending, ConnectURL, 1000000 );

#if 1
	// Create fake control channel
	ServerConnection->CreateChannel( CHTYPE_Control, 1 );
#endif

	// use the same byte format regardless of platform so that the demos are cross platform
	// DEMO_FIXME: This is messing up for some reason, investigate
	//FileAr->SetByteSwapping( true );

	int32 EngineVersion = 0;
	(*FileAr) << EngineVersion;
	(*FileAr) << PlaybackTotalFrames;

	UE_LOG( LogDemo, Log, TEXT( "Starting demo playback with demo. Filename: %s, Frames: %i, Version %i" ), *DemoFilename, PlaybackTotalFrames, EngineVersion );

#if 1
	// Bypass UDemoPendingNetLevel
	FString LevelName;
	(*FileAr) << LevelName;

	FString LoadMapError;

	FURL DemoURL;
	DemoURL.Map = LevelName;

	FWorldContext * WorldContext = GEngine->GetWorldContextFromWorld( World );

	if ( WorldContext == NULL )
	{
		Error = FString::Printf( TEXT( "No world context" ), *DemoFilename );
		UE_LOG( LogDemo, Error, TEXT( "UDemoNetDriver::InitConnect: %s" ), *Error );
		return false;
	}

	World->DemoNetDriver = NULL;
	SetWorld( NULL );

	UDemoPendingNetGame * NewPendingNetGame = new UDemoPendingNetGame( FPostConstructInitializeProperties() );

	NewPendingNetGame->DemoNetDriver = this;

	WorldContext->PendingNetGame = NewPendingNetGame;

	if ( !GEngine->LoadMap( *WorldContext, DemoURL, NewPendingNetGame, LoadMapError ) )
	{
		Error = LoadMapError;
		UE_LOG( LogDemo, Error, TEXT( "UDemoNetDriver::InitConnect: LoadMap failed: failed: %s" ), *Error );
		return false;
	}

	SetWorld( WorldContext->World() );
	WorldContext->World()->DemoNetDriver = this;
	WorldContext->PendingNetGame = NULL;
#endif

	DemoDeltaTime = 0;

	return true;
}

bool UDemoNetDriver::InitListen( FNetworkNotify* InNotify, FURL& ListenURL, bool bReuseAddressAndPort, FString& Error )
{
	if ( !InitBase( false, InNotify, ListenURL, bReuseAddressAndPort, Error ) )
	{
		return false;
	}

	check( World != NULL );

	class AWorldSettings * WorldSettings = World->GetWorldSettings();

	if ( !WorldSettings )
	{
		Error = TEXT( "No WorldSettings!!" );
		return false;
	}

	// Recording, local machine is server, demo stream acts "as if" it's a client.
	UDemoNetConnection* Connection = ConstructObject<UDemoNetConnection>( UDemoNetConnection::StaticClass() );
	Connection->InitConnection( this, USOCK_Open, ListenURL, 1000000 );
	Connection->InitSendBuffer();

	FileAr = IFileManager::Get().CreateFileWriter( *DemoFilename );
	ClientConnections.Add( Connection );

	if( !FileAr )
	{
		Error = FString::Printf( TEXT("Couldn't open demo file %s for writing"), *DemoFilename );//@todo demorec: localize
		return false;
	}

	// use the same byte format regardless of platform so that the demos are cross platform
	//@note: swap on non console platforms as the console archives have byte swapping compiled out by default
	//FileAr->SetByteSwapping(true);

	// write engine version info
	int32 EngineVersion = GEngineNetVersion;
	(*FileAr) << EngineVersion;

	// write placeholder for total frames - will be updated when the demo is stopped
	PlaybackTotalFrames = 0;
	(*FileAr) << PlaybackTotalFrames;

#if 0
	// Create the control channel.
	Connection->CreateChannel( CHTYPE_Control, 1, 0 );

	// Send initial message.
	uint8 IsLittleEndian = uint8( PLATFORM_LITTLE_ENDIAN );
	check( IsLittleEndian == !!IsLittleEndian ); // should only be one or zero
	FNetControlMessage<NMT_Hello>::Send( Connection, IsLittleEndian, GEngineMinNetVersion, GEngineNetVersion, Cast<UGeneralProjectSettings>(UGeneralProjectSettings::StaticClass()->GetDefaultObject())->ProjectID );
	Connection->FlushNet();

	// WelcomePlayer will send the needed map name
	World->WelcomePlayer( Connection );
#else
	// Bypass UDemoPendingNetLevel
	FString LevelName = World->GetCurrentLevel()->GetOutermost()->GetName();
	(*FileAr) << LevelName;
#endif

	// Spawn the demo recording spectator.
	SpawnDemoRecSpectator( Connection );

	DemoDeltaTime = 0;
	LastRecordTime = 0;

	return true;
}

void UDemoNetDriver::TickDispatch( float DeltaSeconds )
{
	Super::TickDispatch( DeltaSeconds );
}

void UDemoNetDriver::TickFlush( float DeltaSeconds )
{
	Super::TickFlush( DeltaSeconds );

	if ( FileAr )
	{
		if ( ClientConnections.Num() > 0 )
		{
			TickDemoRecord( DeltaSeconds );
		}
		else if ( ServerConnection != NULL )
		{
			TickDemoPlayback( DeltaSeconds );
		}
	}
}

void UDemoNetDriver::ProcessRemoteFunction( class AActor* Actor, class UFunction* Function, void* Parameters, struct FOutParmRec* OutParms, struct FFrame* Stack, class UObject* SubObject )
{

}

bool UDemoNetDriver::Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar )
{
	return Super::Exec( InWorld, Cmd, Ar);
}

void UDemoNetDriver::StopDemo()
{
	if ( !ServerConnection && ClientConnections.Num() == 0 )
	{
		UE_LOG( LogDemo, Warning, TEXT( "StopDemo: No demo is playing" ) );
		return;
	}

	UE_LOG( LogDemo, Log, TEXT( "StopDemo: Demo %s stopped at frame %d" ), *DemoFilename, DemoFrameNum );

	if ( !ServerConnection )
	{
		// write the total number of frames in the placeholder at the beginning of the file
		if ( FileAr != NULL && World != NULL )
		{
			PlaybackTotalFrames = DemoFrameNum;
			int32 OldPos = FileAr->Tell();
			FileAr->Seek( sizeof( int32 ) );
			(*FileAr) << PlaybackTotalFrames;
			FileAr->Seek( OldPos );
		}

		// let GC cleanup the object
		if ( ClientConnections.Num() > 0 && ClientConnections[0] != NULL )
		{
			ClientConnections[0]->Close();
			ClientConnections[0]->CleanUp(); // make sure DemoRecSpectator gets destroyed immediately
		}
	}
	else
	{
		// flush out any pending network traffic
		ServerConnection->FlushNet();

#if 0		
		for (int32 i = ServerConnection->OpenChannels.Num() - 1; i >= 0; i--)
		{
			UChannel* OpenChannel = ServerConnection->OpenChannels[i];
			if (OpenChannel != NULL)
			{
				UActorChannel* ActorChannel = Cast< UActorChannel >( OpenChannel );
				if ( ActorChannel != NULL )
				{
					if ( !ActorChannel->GetActor()->IsNetStartupActor() )
					{
						OpenChannel->ConditionalCleanUp();
					}
				}
			}
		}
#endif

		ServerConnection->State = USOCK_Closed;
		ServerConnection->Close();
		ServerConnection->CleanUp(); // make sure DemoRecSpectator gets destroyed immediately
		ServerConnection = NULL;
		
		//GEngine->SetClientTravel( World, TEXT( "?closed" ), TRAVEL_Absolute );
		//SetWorld( NULL );
	}

	delete FileAr;
	FileAr = NULL;

	check( ClientConnections.Num() == 0 );
	check( ServerConnection == NULL );
}

/*-----------------------------------------------------------------------------
Demo Recording tick.
-----------------------------------------------------------------------------*/

static void DemoReplicateActor(AActor* Actor, UNetConnection* Connection, bool IsNetClient)
{
	// All actors marked for replication are assumed to be relevant for demo recording.
	/*
	if
		(	Actor
		&&	((IsNetClient && Actor->bTearOff) || Actor->RemoteRole != ROLE_None || (IsNetClient && Actor->Role != ROLE_None && Actor->Role != ROLE_Authority) || Actor->bForceDemoRelevant)
		&&  (!Actor->bNetTemporary || !Connection->SentTemporaries.Contains(Actor))
		&& (Actor == Connection->PlayerController || Cast<APlayerController>(Actor) == NULL)
		)
	*/
	if ( Actor != NULL && Actor->GetRemoteRole() != ROLE_None && ( Actor == Connection->PlayerController || Cast< APlayerController >( Actor ) == NULL ) )
	{
		// Create a new channel for this actor.
		const bool StartupActor = Actor->IsNetStartupActor();
		UActorChannel* Channel = Connection->ActorChannels.FindRef( Actor );

		if ( !Channel && ( !StartupActor || Connection->ClientHasInitializedLevelFor( Actor ) ) )
		{
			// create a channel if possible
			Channel = (UActorChannel*)Connection->CreateChannel( CHTYPE_Actor, 1 );
			if ( Channel != NULL )
			{
				Channel->SetChannelActor( Actor );
			}
		}

		if ( Channel )
		{
			// Send it out!
			check( !Channel->Closing );

			if ( Channel->IsNetReady( 0 ) )
			{
				Channel->ReplicateActor();
			}
		}
	}
}

void UDemoNetDriver::TickDemoRecord( float DeltaSeconds )
{
	if ( ClientConnections.Num() == 0 )
	{
		return;
	}

	if ( FileAr == NULL )
	{
		return;
	}

	DemoDeltaTime += DeltaSeconds;

	const double CurrentSeconds = FPlatformTime::Seconds();

	const double RECORD_HZ		= CVarDemoRecordHz.GetValueOnGameThread();
	const double RECORD_DELAY	= 1.0 / RECORD_HZ;

	if ( CurrentSeconds - LastRecordTime < RECORD_DELAY )
	{
		return;		// Not enough real-time has passed to record another frame
	}

	LastRecordTime = CurrentSeconds;

	// Save out a frame
	DemoFrameNum++;
	ReplicationFrame++;

	// Save elapsed game time
	*FileAr << DemoDeltaTime;

#if DEMO_CHECKSUMS == 1
	uint32 DeltaTimeChecksum = FCrc::MemCrc32( &DemoDeltaTime, sizeof( DemoDeltaTime ), 0 );
	*FileAr << DeltaTimeChecksum;
#endif

	DemoDeltaTime = 0;

	// Make sure we don't have anything in the buffer for this new frame
	check( ClientConnections[0]->SendBuffer.GetNumBits() == 0 );

	bIsRecordingDemoFrame = true;

	// Dump any queued packets
	UDemoNetConnection * ClientDemoConnection = CastChecked< UDemoNetConnection >( ClientConnections[0] );

	for ( int32 i = 0; i < ClientDemoConnection->QueuedDemoPackets.Num(); i++ )
	{
		ClientDemoConnection->LowLevelSend( (char*)&ClientDemoConnection->QueuedDemoPackets[i].Data[0], ClientDemoConnection->QueuedDemoPackets[i].Data.Num() );
	}

	ClientDemoConnection->QueuedDemoPackets.Empty();

	const bool IsNetClient = ( GetWorld()->GetNetDriver() != NULL && GetWorld()->GetNetDriver()->GetNetMode() == NM_Client );

	DemoReplicateActor( World->GetWorldSettings(), ClientConnections[0], IsNetClient );

	for ( int32 i = 0; i < World->NetworkActors.Num(); i++ )
	{
		AActor * Actor = World->NetworkActors[i];

		Actor->PreReplication( *FindOrCreateRepChangedPropertyTracker( Actor ).Get() );
		DemoReplicateActor( Actor, ClientConnections[0], IsNetClient );
	}

	// Make sure nothing is left over
	ClientConnections[0]->FlushNet();

	check( ClientConnections[0]->SendBuffer.GetNumBits() == 0 );

	bIsRecordingDemoFrame = false;

	// Write a count of 0 to signal the end of the frame
	int32 EndCount = 0;

	*FileAr << EndCount;
}

bool UDemoNetDriver::ReadDemoFrame()
{
	if ( FileAr->AtEnd() || FileAr->IsError() )
	{
		StopDemo();
		return false;
	}

	const int32 OldFilePos = FileAr->Tell();

	float ServerDeltaTime;

	// Peek at the next demo delta time, and see if we should process this frame
	*FileAr << ServerDeltaTime;

#if DEMO_CHECKSUMS == 1
	{
		uint32 ServerDeltaTimeCheksum = 0;
		*FileAr << ServerDeltaTimeCheksum;

		const uint32 DeltaTimeChecksum = FCrc::MemCrc32( &ServerDeltaTime, sizeof( ServerDeltaTime ), 0 );

		if ( DeltaTimeChecksum != ServerDeltaTimeCheksum )
		{
			UE_LOG( LogDemo, Error, TEXT( "UDemoNetDriver::ReadDemoFrame: DeltaTimeChecksum != ServerDeltaTimeCheksum" ) );
			StopDemo();
			return false;
		}
	}
#endif

	if ( FileAr->IsError() )
	{
		UE_LOG( LogDemo, Error, TEXT( "UDemoNetDriver::ReadDemoFrame: Failed to read demo ServerDeltaTime" ) );
		StopDemo();
		return false;
	}

	if ( DemoDeltaTime - ServerDeltaTime < 0 )//&& ServerConnection->State != USOCK_Pending )
	{
		// Not enough time has passed to read another frame
		FileAr->Seek( OldFilePos );
		return false;
	}

	DemoDeltaTime -= ServerDeltaTime;

	while ( true )
	{
		uint8 ReadBuffer[ MAX_DEMO_READ_WRITE_BUFFER ];

		int32 PacketBytes;

		*FileAr << PacketBytes;

		if ( FileAr->IsError() )
		{
			UE_LOG( LogDemo, Error, TEXT( "UDemoNetDriver::ReadDemoFrame: Failed to read demo PacketBytes" ) );
			StopDemo();
			return false;
		}

		if ( PacketBytes == 0 )
		{
			break;
		}

		if ( PacketBytes > sizeof( ReadBuffer ) )
		{
			UE_LOG( LogDemo, Error, TEXT( "UDemoNetDriver::ReadDemoFrame: PacketBytes > sizeof( ReadBuffer )" ) );
			StopDemo();
			return false;
		}

		// Read data from file.
		FileAr->Serialize( ReadBuffer, PacketBytes );

		if ( FileAr->IsError() )
		{
			UE_LOG( LogDemo, Error, TEXT( "UDemoNetDriver::ReadDemoFrame: Failed to read demo file packet" ) );
			StopDemo();
			return false;
		}

#if DEMO_CHECKSUMS == 1
		{
			uint32 ServerChecksum = 0;
			*FileAr << ServerChecksum;

			const uint32 Checksum = FCrc::MemCrc32( ReadBuffer, PacketBytes, 0 );

			if ( Checksum != ServerChecksum )
			{
				UE_LOG( LogDemo, Error, TEXT( "UDemoNetDriver::ReadDemoFrame: Checksum != ServerChecksum" ) );
				StopDemo();
				return false;
			}
		}
#endif

		// Process incoming packet.
		ServerConnection->ReceivedRawPacket( ReadBuffer, PacketBytes );

		if ( ServerConnection == NULL || ServerConnection->State == USOCK_Closed )
		{
			// Something we received resulted in the demo being stopped
			UE_LOG( LogDemo, Error, TEXT( "UDemoNetDriver::ReadDemoFrame: ReceivedRawPacket closed connection" ) );
			StopDemo();
			return false;
		}
	}

	return true;
}

void UDemoNetDriver::TickDemoPlayback( float DeltaSeconds )
{
	if ( ServerConnection == NULL || ServerConnection->State == USOCK_Closed )
	{
		StopDemo();
		return;
	}

	DemoDeltaTime += DeltaSeconds;

	while ( true )
	{
		// Read demo frames until we are caught up
		if ( !ReadDemoFrame() )
		{
			break;
		}

		DemoFrameNum++;
	}
}

void UDemoNetDriver::SpawnDemoRecSpectator( UNetConnection* Connection )
{
	check( Connection != NULL );

	UClass* C = StaticLoadClass( AActor::StaticClass(), NULL, *DemoSpectatorClass, NULL, LOAD_None, NULL );
	APlayerController* Controller = World->SpawnActor<APlayerController>( C );

	for ( FActorIterator It( World ); It; ++It)
	{
		if ( It->IsA( APlayerStart::StaticClass() ) )
		{
			Controller->SetInitialLocationAndRotation( It->GetActorLocation(), It->GetActorRotation() );
			break;
		}
	}

	Controller->SetReplicates( true );
	Controller->SetAutonomousProxy( true );

	Controller->SetPlayer( Connection );
}

/*-----------------------------------------------------------------------------
	UDemoNetConnection.
-----------------------------------------------------------------------------*/

UDemoNetConnection::UDemoNetConnection( const class FPostConstructInitializeProperties& PCIP ) : Super( PCIP )
{
	MaxPacket = 512;
	InternalAck = true;
}

void UDemoNetConnection::InitConnection( UNetDriver* InDriver, EConnectionState InState, const FURL& InURL, int32 InConnectionSpeed )
{
	// default implementation
	Super::InitConnection( InDriver, InState, InURL, InConnectionSpeed );

	InitSendBuffer();

	// the driver must be a DemoRecording driver (GetDriver makes assumptions to avoid Cast'ing each time)
	check( InDriver->IsA( UDemoNetDriver::StaticClass() ) );
}

FString UDemoNetConnection::LowLevelGetRemoteAddress( bool bAppendPort )
{
	return TEXT( "UDemoNetConnection" );
}

void UDemoNetConnection::LowLevelSend( void* Data, int32 Count )
{
	if ( Count == 0 )
	{
		UE_LOG( LogDemo, Warning, TEXT( "UDemoNetConnection::LowLevelSend: Ignoring empty packet." ) );
		return;
	}

	if ( Count > MAX_DEMO_READ_WRITE_BUFFER )
	{
		UE_LOG( LogDemo, Fatal, TEXT( "UDemoNetConnection::LowLevelSend: Count > MAX_DEMO_READ_WRITE_BUFFER." ) );
	}

	if ( !GetDriver()->ServerConnection && GetDriver()->FileAr )
	{
		// If we're outside of an official demo frame, we need to queue this up or it will throw off the stream
		if ( !GetDriver()->bIsRecordingDemoFrame )
		{
			FQueuedDemoPacket & B = *( new( QueuedDemoPackets )FQueuedDemoPacket );
			B.Data.AddUninitialized( Count );
			FMemory::Memcpy( B.Data.GetData(), Data, Count );
			return;
		}

		*GetDriver()->FileAr << Count;
		GetDriver()->FileAr->Serialize( Data, Count );
		
#if DEMO_CHECKSUMS == 1
		uint32 Checksum = FCrc::MemCrc32( Data, Count, 0 );
		*GetDriver()->FileAr << Checksum;
#endif
	}
}

FString UDemoNetConnection::LowLevelDescribe()
{
	return TEXT( "Demo recording/playback driver connection" );
}

int32 UDemoNetConnection::IsNetReady( bool Saturate )
{
	return 1;
}

void UDemoNetConnection::FlushNet( bool bIgnoreSimulation )
{
	// in playback, there is no data to send except
	// channel closing if an error occurs.
	if ( GetDriver()->ServerConnection != NULL )
	{
		InitSendBuffer();
	}
	else
	{
		Super::FlushNet( bIgnoreSimulation );
	}
}

void UDemoNetConnection::HandleClientPlayer( APlayerController* PC, UNetConnection* NetConnection )
{
	Super::HandleClientPlayer( PC, NetConnection );

	for ( FActorIterator It( Driver->World ); It; ++It)
	{
		if ( It->IsA( APlayerStart::StaticClass() ) )
		{
			PC->SetInitialLocationAndRotation( It->GetActorLocation(), It->GetActorRotation() );

			if ( PC->GetPawn() )
			{
				PC->GetPawn()->TeleportTo( It->GetActorLocation(), It->GetActorRotation(), false, true );
			}
			break;
		}
	}
}

bool UDemoNetConnection::ClientHasInitializedLevelFor(const UObject* TestObject)
{
	// there is no way to know when demo playback will load levels,
	// so we assume it always is anytime after initial replication and rely on the client to load it in time to make the reference work
	return ( GetDriver()->DemoFrameNum > 2 || Super::ClientHasInitializedLevelFor( TestObject ) );
}

/*-----------------------------------------------------------------------------
	UDemoPendingNetGame.
-----------------------------------------------------------------------------*/

UDemoPendingNetGame::UDemoPendingNetGame( const class FPostConstructInitializeProperties& PCIP ) : Super( PCIP )
{
}
