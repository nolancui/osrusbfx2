/*++

Copyright (c) Microsoft Corporation, All Rights Reserved

Module Name:

    queue.cpp

Abstract:

    This file implements the I/O queue interface and performs
    the read/write/ioctl operations.

Environment:

    Windows User-Mode Driver Framework (WUDF)

--*/

#include "internal.h"
#include "ReadWriteQueue.tmh"

VOID
CMyReadWriteQueue::OnCompletion(
    __in IWDFIoRequest*                 pWdfRequest,
    __in IWDFIoTarget*                  pIoTarget,
    __in IWDFRequestCompletionParams*   pParams,
    __in PVOID                          pContext
    )
{
    UNREFERENCED_PARAMETER(pIoTarget);
    UNREFERENCED_PARAMETER(pContext);

    pWdfRequest->CompleteWithInformation(
        pParams->GetCompletionStatus(),
        pParams->GetInformation()
        );
}

void
CMyReadWriteQueue::ForwardFormattedRequest(
    __in IWDFIoRequest*                         pRequest,
    __in IWDFIoTarget*                          pIoTarget
    )
{
    //
    //First set the completion callback
    //

    IRequestCallbackRequestCompletion * pCompletionCallback = NULL;
    HRESULT hrQI = this->QueryInterface(IID_PPV_ARGS(&pCompletionCallback));
    WUDF_TEST_DRIVER_ASSERT(SUCCEEDED(hrQI) && (NULL != pCompletionCallback));

    pRequest->SetCompletionCallback(
        pCompletionCallback,
        NULL
        );

    pCompletionCallback->Release();
    pCompletionCallback = NULL;

    //
    //Send down the request
    //

    HRESULT hrSend = S_OK;
    hrSend = pRequest->Send(pIoTarget,
                            0,  //flags
                            0); //timeout

    if (FAILED(hrSend))
    {
        pRequest->CompleteWithInformation(hrSend, 0);
    }

    return;
}


CMyReadWriteQueue::CMyReadWriteQueue(
    __in PCMyDevice Device
    ) :
    CMyQueue(Device)
{
}

//
// Queue destructor.
// Free up the buffer, wait for thread to terminate and
//

CMyReadWriteQueue::~CMyReadWriteQueue(
    VOID
    )
/*++

Routine Description:


    IUnknown implementation of Release

Aruments:


Return Value:

    ULONG (reference count after Release)

--*/
{
    TraceEvents(TRACE_LEVEL_INFORMATION,
                TEST_TRACE_QUEUE,
                "%!FUNC! Entry"
                );

}


HRESULT
STDMETHODCALLTYPE
CMyReadWriteQueue::QueryInterface(
    __in REFIID InterfaceId,
    __deref_out PVOID *Object
    )
/*++

Routine Description:


    Query Interface

Aruments:

    Follows COM specifications

Return Value:

    HRESULT indicatin success or failure

--*/
{
    HRESULT hr;


    if (IsEqualIID(InterfaceId, __uuidof(IQueueCallbackWrite)))
    {
        hr = S_OK;
        *Object = QueryIQueueCallbackWrite();
    }
    else if (IsEqualIID(InterfaceId, __uuidof(IQueueCallbackRead)))
    {
        hr = S_OK;
        *Object = QueryIQueueCallbackRead();
    }
    else if (IsEqualIID(InterfaceId, __uuidof(IRequestCallbackRequestCompletion)))
    {
        hr = S_OK;
        *Object = QueryIRequestCallbackRequestCompletion();
    }
    else if (IsEqualIID(InterfaceId, __uuidof(IQueueCallbackIoStop)))
    {
        hr = S_OK;
        *Object = QueryIQueueCallbackIoStop();
    }
    else
    {
        hr = CMyQueue::QueryInterface(InterfaceId, Object);
    }

    return hr;
}

//
// Initialize
//

HRESULT
CMyReadWriteQueue::CreateInstance(
    __in PCMyDevice Device,
    __out PCMyReadWriteQueue *Queue
    )
/*++

Routine Description:


    CreateInstance creates an instance of the queue object.

Aruments:

    ppUkwn - OUT parameter is an IUnknown interface to the queue object

Return Value:

    HRESULT indicatin success or failure

--*/
{
    PCMyReadWriteQueue queue;
    HRESULT hr = S_OK;

    queue = new CMyReadWriteQueue(Device);

    if (NULL == queue)
    {
        hr = E_OUTOFMEMORY;
    }

    //
    // Call the queue callback object to initialize itself.  This will create
    // its partner queue framework object.
    //

    if (SUCCEEDED(hr))
    {
        hr = queue->Initialize();
    }

    if (SUCCEEDED(hr))
    {
        *Queue = queue;
    }
    else
    {
        SAFE_RELEASE(queue);
    }

    return hr;
}

HRESULT
CMyReadWriteQueue::Initialize(
    )
{
    HRESULT hr;

    bool powerManaged;
    
    //
    // First initialize the base class.  This will create the partner FxIoQueue
    // object and setup automatic forwarding of I/O controls.
    //

    //
    // The framework (UMDF) will not deliver a 
    // request to the driver that arrives on a power-managed queue, unless  
    // the device is in a powered-up state. If you receive a request on a 
    // power-managed queue after the device has idled out,
    // the framework will not be able to power-up and present the request
    // to the driver unless it is the power policy owner (PPO).
    // Hence if the driver is not the PPO and it sits above the PPO, 
    // it must not use power-managed queues
    // 

#if defined(_NOT_POWER_POLICY_OWNER_)
    // 
    // If WinUsb.sys is the PPO we should not use power managed
    // queues. This way the driver recieves a request even when the 
    // device has idled out. The driver then forwards it down to WinUsb.sys
    // If necessary WinUsb takes the required steps to power up the device.
    // 
    
    powerManaged = false;
#else
    //
    // If we are implementing Idle/Wake where UMDF is the 
    // power policy owner(PPO) we can use power managed queues. 
    // If the device has idled out it will be powered up by the 
    // framework when a request is recieved on such a queue.
    //
    
    powerManaged = true;
#endif  

    hr = __super::Initialize(WdfIoQueueDispatchParallel, 
                            true, 
                            powerManaged
                            );

    //
    // return the status.
    //

    return hr;
}

STDMETHODIMP_ (void)
CMyReadWriteQueue::OnWrite(
    __in IWDFIoQueue *pWdfQueue,
    __in IWDFIoRequest *pWdfRequest,
    __in SIZE_T BytesToWrite
    )
/*++

Routine Description:


    Write dispatch routine
    IQueueCallbackWrite

Aruments:

    pWdfQueue - Framework Queue instance
    pWdfRequest - Framework Request  instance
    BytesToWrite - Lenth of bytes in the write buffer

    Allocate and copy data to local buffer
Return Value:

    VOID

--*/
{
    UNREFERENCED_PARAMETER(pWdfQueue);

    TraceEvents(TRACE_LEVEL_INFORMATION,
                TEST_TRACE_QUEUE,
                "%!FUNC!: Queue %p Request %p BytesToTransfer %d\n",
                this,
                pWdfRequest,
                (ULONG)(ULONG_PTR)BytesToWrite
                );

    HRESULT hr = S_OK;
    IWDFMemory * pInputMemory = NULL;
    IWDFUsbTargetPipe * pOutputPipe = m_Device->GetOutputPipe();

    pWdfRequest->GetInputMemory(&pInputMemory);

    hr = pOutputPipe->FormatRequestForWrite(
                                pWdfRequest,
                                NULL, //pFile
                                pInputMemory,
                                NULL, //Memory offset
                                NULL  //DeviceOffset
                                );

    if (FAILED(hr))
    {
        pWdfRequest->Complete(hr);
    }
    else
    {
        ForwardFormattedRequest(pWdfRequest, pOutputPipe);
    }

    SAFE_RELEASE(pInputMemory);

    return;
}

STDMETHODIMP_ (void)
CMyReadWriteQueue::OnRead(
    __in IWDFIoQueue *pWdfQueue,
    __in IWDFIoRequest *pWdfRequest,
    __in SIZE_T BytesToRead
    )
/*++

Routine Description:


    Read dispatch routine
    IQueueCallbackRead

Aruments:

    pWdfQueue - Framework Queue instance
    pWdfRequest - Framework Request  instance
    BytesToRead - Lenth of bytes in the read buffer

    Copy available data into the read buffer
Return Value:

    VOID

--*/
{
    UNREFERENCED_PARAMETER(pWdfQueue);

    TraceEvents(TRACE_LEVEL_INFORMATION,
                TEST_TRACE_QUEUE,
                "%!FUNC!: Queue %p Request %p BytesToTransfer %d\n",
                this,
                pWdfRequest,
                (ULONG)(ULONG_PTR)BytesToRead
                );

    HRESULT hr = S_OK;
    IWDFMemory * pOutputMemory = NULL;

    pWdfRequest->GetOutputMemory(&pOutputMemory);

    hr = m_Device->GetInputPipe()->FormatRequestForRead(
                                pWdfRequest,
                                NULL, //pFile
                                pOutputMemory,
                                NULL, //Memory offset
                                NULL  //DeviceOffset
                                );

    if (FAILED(hr))
    {
        pWdfRequest->Complete(hr);
    }
    else
    {
        ForwardFormattedRequest(pWdfRequest, m_Device->GetInputPipe());
    }

    SAFE_RELEASE(pOutputMemory);

    return;
}

STDMETHODIMP_ (void)
CMyReadWriteQueue::OnIoStop(
    __in IWDFIoQueue *   pWdfQueue,
    __in IWDFIoRequest * pWdfRequest,
    __in ULONG           ActionFlags
    )
{
    UNREFERENCED_PARAMETER(pWdfQueue);


    //
    // The driver owns the request and no locking constraint is safe for 
    // the queue callbacks
    //

    if (ActionFlags == WdfRequestStopActionSuspend )
    {
        //
        // UMDF does not support an equivalent to WdfRequestStopAcknowledge.
        //
        // Cancel the request so that the power management operation can continue.
        //
        // NOTE: if cancelling the request would have an adverse affect and if the
        //       requests are expected to complete very quickly then leaving the
        //       request running may be a better option.
        //

        pWdfRequest->CancelSentRequest();
    }
    else if(ActionFlags == WdfRequestStopActionPurge)
    {
        //
        // Cancel the sent request since we are asked to purge the request
        //

        pWdfRequest->CancelSentRequest();
    }

    return;
}

