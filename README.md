<p align="center">
  <h1 align="center">VmxHijack</h1>
  <h3><p align="center">
    Header-only VMWare Backdoor API Implementation & Effortless VMX Patcher for Custom Guest-to-Host RPCs
  </p></h3>
  </br>
</p>

# Sample

```cpp

// --- RPC Server Code (VmxHijack/vmx.hpp)
//
bool vmx_log_handler(
	uint64_t vcpuid, void* vcpuctx,
	const char* data, uint32_t length,
	const void** out, uint32_t* out_length )
{
    // Insert the message prefix.
    //
	std::string msg = "[vmx] vcpu-" + std::to_string( vcpuid ) + ": ";
	msg.insert( msg.end(), data, data + length );

	// Print onto the host console and DebugView.
	//
	OutputDebugStringA( msg.c_str() );
	logger::print<CON_BRG>( "%s\n", msg.c_str() );

	// Write dummy output.
	//
	*out = "OK";
	*out_length = 2;
	return true;
}



// --- RPC Client Code (Any guest application/driver/hypervisor)
//
extern "C" int32_t DriverEntry()
{
	DbgPrint( "=> %s\n", vmx::send( "Hello from guest Ring0 to Host!" )->c_str() );
	return -1;
}
```

<img height="348" src="https://i.imgur.com/OHUClOq.png">
