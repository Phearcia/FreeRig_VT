#include "FreeRigPhoneServer.h"
#include "FreeRigComponent.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Common/TcpSocketBuilder.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

UFreeRigPhoneServer::UFreeRigPhoneServer()
{
    PrimaryComponentTick.bCanEverTick = true;
    ListenerSocket = nullptr;
}

UFreeRigPhoneServer::~UFreeRigPhoneServer()
{
    StopServer();
}

void UFreeRigPhoneServer::StartServer(int32 Port)
{
    if (bIsRunning)
    {
        StopServer();
    }

    LocalIP = GetLocalIPAddress();
    CurrentPort = Port;

    ListenerSocket = FTcpSocketBuilder(TEXT("FreeRigTcpServer"))
        .AsReusable()
        .BoundToPort(Port)
        .Listening(8);

    if (!ListenerSocket)
    {
        UE_LOG(LogTemp, Error, TEXT("FreeRig: Failed to create TCP socket on port %d"), Port);
        return;
    }

    bIsRunning = true;

    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("FREE RIG PHONE SERVER"));
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
    UE_LOG(LogTemp, Warning, TEXT("Server started on http://%s:%d"), *LocalIP, Port);
    UE_LOG(LogTemp, Warning, TEXT("========================================"));
}

void UFreeRigPhoneServer::StopServer()
{
    if (ListenerSocket)
    {
        ListenerSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenerSocket);
        ListenerSocket = nullptr;
    }

    for (FSocket* Client : ClientSockets)
    {
        if (Client)
        {
            Client->Close();
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Client);
        }
    }
    ClientSockets.Empty();

    bIsRunning = false;
    UE_LOG(LogTemp, Log, TEXT("FreeRig: Server stopped"));
}

void UFreeRigPhoneServer::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bIsRunning || !ListenerSocket)
    {
        return;
    }

    // Accept new connections
    AcceptConnections();

    // Receive data from clients
    for (int32 i = ClientSockets.Num() - 1; i >= 0; i--)
    {
        FSocket* Client = ClientSockets[i];
        if (!Client)
        {
            ClientSockets.RemoveAt(i);
            continue;
        }

        uint32 PendingDataSize = 0;
        if (Client->HasPendingData(PendingDataSize))
        {
            TArray<uint8> ReceivedData;
            ReceivedData.SetNumUninitialized(PendingDataSize);
            int32 BytesRead = 0;

            if (Client->Recv(ReceivedData.GetData(), PendingDataSize, BytesRead))
            {
                FString ReceivedMessage = FString(UTF8_TO_TCHAR((const char*)ReceivedData.GetData()));

                TSharedPtr<FJsonObject> JsonData;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReceivedMessage);

                if (FJsonSerializer::Deserialize(Reader, JsonData) && JsonData.IsValid())
                {
                    if (TargetRigComponent.IsValid())
                    {
                        float LeftEyeOpen = JsonData->HasField(TEXT("leftEyeOpen")) ? JsonData->GetNumberField(TEXT("leftEyeOpen")) : 0.5f;
                        float RightEyeOpen = JsonData->HasField(TEXT("rightEyeOpen")) ? JsonData->GetNumberField(TEXT("rightEyeOpen")) : 0.5f;
                        float MouthOpen = JsonData->HasField(TEXT("mouthOpen")) ? JsonData->GetNumberField(TEXT("mouthOpen")) : 0.0f;
                        float MouthSmile = JsonData->HasField(TEXT("mouthSmile")) ? JsonData->GetNumberField(TEXT("mouthSmile")) : 0.0f;
                        float BrowUp = JsonData->HasField(TEXT("browUp")) ? JsonData->GetNumberField(TEXT("browUp")) : 0.0f;
                        float Blink = JsonData->HasField(TEXT("blink")) ? JsonData->GetNumberField(TEXT("blink")) : 0.0f;

                        TargetRigComponent->UpdateFromFaceTracking(LeftEyeOpen, RightEyeOpen, MouthOpen, MouthSmile, BrowUp, Blink);
                    }
                }
            }
            else
            {
                Client->Close();
                ClientSockets.RemoveAt(i);
                UE_LOG(LogTemp, Log, TEXT("FreeRig: Client disconnected. Remaining: %d"), ClientSockets.Num());
            }
        }
    }
}

FString UFreeRigPhoneServer::GetConnectionURL() const
{
    return FString::Printf(TEXT("http://%s:%d"), *LocalIP, CurrentPort);
}

void UFreeRigPhoneServer::SetTargetRigComponent(UFreeRigComponent* InRigComponent)
{
    TargetRigComponent = InRigComponent;
}

FString UFreeRigPhoneServer::GetLocalIPAddress()
{
    bool bCanBindAll = false;
    TSharedPtr<FInternetAddr> Addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalHostAddr(*GLog, bCanBindAll);
    return Addr.IsValid() ? Addr->ToString(false) : TEXT("127.0.0.1");
}

void UFreeRigPhoneServer::AcceptConnections()
{
    bool bPending = false;
    if (ListenerSocket && ListenerSocket->HasPendingConnection(bPending) && bPending)
    {
        FSocket* ClientSocket = ListenerSocket->Accept(TEXT("FreeRigClient"));
        if (!ClientSocket)
            return;

        uint32 PendingData = 0;
        bool bIsHttp = false;

        if (ClientSocket->HasPendingData(PendingData))
        {
            TArray<uint8> RequestData;
            RequestData.SetNumUninitialized(PendingData);
            int32 BytesRead = 0;
            ClientSocket->Recv(RequestData.GetData(), PendingData, BytesRead);

            FString Request = FString(UTF8_TO_TCHAR((const char*)RequestData.GetData()));

            // HTTP GET always starts with 'G', even when fragmented
            if (Request.StartsWith(TEXT("G")))
            {
                bIsHttp = true;
            }
        }

        if (bIsHttp)
        {
            SendHTMLPage(ClientSocket);
            ClientSocket->Close();
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
            return;
        }

        // Real WebSocket client
        ClientSockets.Add(ClientSocket);
        UE_LOG(LogTemp, Log, TEXT("FreeRig: WebSocket client connected! Total: %d"), ClientSockets.Num());

        CloseQRWindow();
    }
}

TSharedPtr<SWindow> UFreeRigPhoneServer::QRWindow = nullptr;

void UFreeRigPhoneServer::CloseQRWindow()
{
    if (!QRWindow.IsValid())
        return;

    AsyncTask(ENamedThreads::GameThread, []()
        {
            if (UFreeRigPhoneServer::QRWindow.IsValid())
            {
                UFreeRigPhoneServer::QRWindow->RequestDestroyWindow();
                UFreeRigPhoneServer::QRWindow.Reset();
                UE_LOG(LogTemp, Log, TEXT("FreeRig: QR window closed safely on game thread"));
            }
        });
}

void UFreeRigPhoneServer::SendHTMLPage(FSocket* ClientSocket)
{
    FString HtmlPage = TEXT(R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>FreeRig - Phone Controller</title>
    <style>
        body { margin: 0; background: #0a0e27; color: #fff; font-family: sans-serif; text-align: center; }
        video { width: 100%; max-width: 480px; margin-top: 10px; border-radius: 8px; }
        #status { margin-top: 10px; font-size: 14px; }
        .params { position: fixed; top: 20px; right: 20px; background: rgba(0,0,0,0.7); padding: 10px; border-radius: 8px; text-align: left; font-size: 12px; }
        .param-row { margin: 5px 0; }
        .param-value { color: #4CAF50; font-weight: bold; }
    </style>
</head>
<body>
    <h2>FreeRig Phone Controller</h2>
    <p>Grant camera access and make faces!</p>
    <video id="video" autoplay playsinline muted></video>
    <div id="status">Initializing...</div>
    <div class="params">
        <div class="param-row">Left Eye: <span id="left-eye" class="param-value">0.00</span></div>
        <div class="param-row">Right Eye: <span id="right-eye" class="param-value">0.00</span></div>
        <div class="param-row">Mouth: <span id="mouth" class="param-value">0.00</span></div>
        <div class="param-row">Smile: <span id="smile" class="param-value">0.00</span></div>
        <div class="param-row">Brow: <span id="brow" class="param-value">0.00</span></div>
        <div class="param-row">Blink: <span id="blink" class="param-value">0.00</span></div>
    </div>
    
    <script src="https://cdn.jsdelivr.net/npm/@mediapipe/face_mesh/face_mesh.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/@mediapipe/camera_utils/camera_utils.js"></script>
    <script>
        const statusEl = document.getElementById('status');
        let lastSendTime = 0;
        const SEND_INTERVAL = 33;
        let socket = null;
        
        function updateStatus(msg) {
            statusEl.textContent = msg;
            console.log(msg);
        }
        
        function connectWebSocket() {
            const wsUrl = `ws://${window.location.hostname}:8081`;
            socket = new WebSocket(wsUrl);
            socket.onopen = () => updateStatus('Connected to rig!');
            socket.onclose = () => {
                updateStatus('Disconnected. Reconnecting...');
                setTimeout(connectWebSocket, 3000);
            };
            socket.onerror = (err) => updateStatus('WebSocket error');
        }
        
        async function sendFaceData(landmarks) {
            if (!socket || socket.readyState !== WebSocket.OPEN) return;
            
            const now = Date.now();
            if (now - lastSendTime < SEND_INTERVAL) return;
            lastSendTime = now;
            
            const leftEyeUpper = landmarks[159];
            const leftEyeLower = landmarks[145];
            const leftEyeOpen = Math.min(1.0, Math.max(0, (leftEyeLower.y - leftEyeUpper.y) * 8));
            
            const rightEyeUpper = landmarks[386];
            const rightEyeLower = landmarks[374];
            const rightEyeOpen = Math.min(1.0, Math.max(0, (rightEyeLower.y - rightEyeUpper.y) * 8));
            
            const mouthUpper = landmarks[13];
            const mouthLower = landmarks[14];
            const mouthOpen = Math.min(1.0, Math.max(0, (mouthLower.y - mouthUpper.y) * 5));
            
            const leftCorner = landmarks[61];
            const rightCorner = landmarks[291];
            const smile = (leftCorner.y + rightCorner.y) / 2;
            const mouthSmile = Math.min(1.0, Math.max(0, (0.5 - smile) * 4));
            
            const leftBrow = landmarks[107];
            const rightBrow = landmarks[336];
            const browUp = Math.min(1.0, Math.max(0, (0.4 - (leftBrow.y + rightBrow.y) / 2) * 5));
            
            const blink = Math.min(1.0, Math.max(0, 1 - (leftEyeOpen + rightEyeOpen) / 2));
            
            document.getElementById('left-eye').textContent = leftEyeOpen.toFixed(2);
            document.getElementById('right-eye').textContent = rightEyeOpen.toFixed(2);
            document.getElementById('mouth').textContent = mouthOpen.toFixed(2);
            document.getElementById('smile').textContent = mouthSmile.toFixed(2);
            document.getElementById('brow').textContent = browUp.toFixed(2);
            document.getElementById('blink').textContent = blink.toFixed(2);
            
            const data = {
                leftEyeOpen: leftEyeOpen,
                rightEyeOpen: rightEyeOpen,
                mouthOpen: mouthOpen,
                mouthSmile: mouthSmile,
                browUp: browUp,
                blink: blink
            };
            
            socket.send(JSON.stringify(data));
        }
        
        async function initFaceMesh() {
            connectWebSocket();
            
            const video = document.getElementById('video');
            const faceMesh = new FaceMesh({
                locateFile: (file) => `https://cdn.jsdelivr.net/npm/@mediapipe/face_mesh/${file}`
            });
            
            faceMesh.setOptions({
                maxNumFaces: 1,
                refineLandmarks: true,
                minDetectionConfidence: 0.5,
                minTrackingConfidence: 0.5
            });
            
            faceMesh.onResults((results) => {
                if (video.videoWidth && video.videoHeight) {
                    if (results.multiFaceLandmarks && results.multiFaceLandmarks.length > 0) {
                        sendFaceData(results.multiFaceLandmarks[0]);
                        updateStatus('🎭 Face tracking active!');
                    }
                }
            });
            
            const camera = new Camera(video, {
                onFrame: async () => { await faceMesh.send({ image: video }); },
                width: 640,
                height: 480
            });
            camera.start();
        }
        
        navigator.mediaDevices.getUserMedia({ video: { facingMode: 'user' } })
            .then(() => {
                updateStatus('📷 Camera ready, loading face mesh...');
                initFaceMesh();
            })
            .catch(err => {
                updateStatus('❌ Camera permission denied');
                console.error(err);
            });
    </script>
</body>
</html>
    )");

    FString HttpResponse = FString::Printf(
        TEXT("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s"),
        HtmlPage.Len(),
        *HtmlPage
    );

    int32 Sent = 0;
    ClientSocket->Send((uint8*)TCHAR_TO_UTF8(*HttpResponse), HttpResponse.Len(), Sent);
}