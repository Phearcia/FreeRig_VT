#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sockets.h"
#include "FreeRigPhoneServer.generated.h"

class UFreeRigComponent;
class INetworkingWebSocket;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class FREERIG_VT_API UFreeRigPhoneServer : public UActorComponent
{
    GENERATED_BODY()

public:

    FSimpleMulticastDelegate OnClientConnected;

    static TSharedPtr<SWindow> QRWindow;
    static void CloseQRWindow();
    
    UFreeRigPhoneServer();
    virtual ~UFreeRigPhoneServer();

    UFUNCTION(BlueprintCallable, Category = "FreeRig|Phone")
    void StartServer(int32 Port = 8080);

    UFUNCTION(BlueprintCallable, Category = "FreeRig|Phone")
    void StopServer();

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "FreeRig|Phone")
    FString GetConnectionURL() const;

    UFUNCTION(BlueprintCallable, Category = "FreeRig|Phone")
    void SetTargetRigComponent(UFreeRigComponent* InRigComponent);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "FreeRig|Phone")
    int32 GetConnectedClientCount() const { return ConnectedClients.Num(); }

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    TArray<class FWebSocket*> ConnectedClients;
    FSocket* ListenerSocket;
    TArray<FSocket*> ClientSockets;

    UPROPERTY()
    TWeakObjectPtr<UFreeRigComponent> TargetRigComponent;

    FString LocalIP;
    int32 CurrentPort = 8080;
    bool bIsRunning = false;

    FString GetLocalIPAddress();
    FString GenerateHTMLPage();
    void AcceptConnections();
    void SendHTMLPage(FSocket* ClientSocket);

    void OnWebSocketClientConnected(INetworkingWebSocket* ClientWebSocket);
    void OnWebSocketPacketReceived(void* Data, int32 Size);
    void OnWebSocketClientClosed();
    void ProcessFaceData(const TSharedPtr<class FJsonObject>& JsonData);
    void SendToAllClients(const FString& Message);

};