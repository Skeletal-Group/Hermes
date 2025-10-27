//
// File:
//     Hermes.cpp
// 
// Abstract:
//     A fast, reliable and refined covert timing channel proof of concept
//     for Windows systems. This proof of concept is derived from a much earlier 
//     proof of concept you can find here: https://github.com/Peribunt/CTC
//     It contains many improvements, and there is still room for more.
// 
//     This proof of concept was made strictly for educational purposes, we are
//     in no way responsible for any software that might implement this functionality
//     for any other purpose.
//
#include "Hermes.h"

#pragma warning( push )
#pragma warning( disable : 6385 )
#pragma warning( disable : 6386 )

#define HERMES_TRANSMIT_TIMEOUT     1000000
#define HERMES_TRANSMIT_FLUSH_COUNT 1000

#define HERMES_EXECUTABLE __declspec( allocate( ".text" ) )
#define HERMES_NOINLINE   __declspec( noinline )
#define HERMES_INLINE     __forceinline

#pragma pack( push, 1 )
typedef union _HERMES_TRANSMIT_BLOCK
{
	UINT64 AsUInt[ 5 ];

	struct
	{
		UINT64 Data[ 2 ];
		UINT32 Position;
		UINT32 Length;
		UINT64 Checksum;
		UINT64 Acknowledgement;
	};
}HERMES_TRANSMIT_BLOCK, *PHERMES_TRANSMIT_BLOCK;
#pragma pack( pop )

CONST __m128i HermesTransmitStartMagic = _mm_set_epi64x( 0x7C0DE000CAFECAFE, 0xDEAFDEAFCAFECAFE );
CONST __m128i HermesTransmitEndMagic   = _mm_set_epi64x( 0x7C0DE001CAFECAFE, 0xCAFECAFEDEAFDEAF );

//
// The base virtual address of the memory region whose cache lines 
// will be used for our communication channel
//
UINT8* HermesCacheLines = NULL;

//
// The size of a single cache line on the current executing CPU
//
UINT64 HermesLineSize = 0;

#pragma code_seg( push, ".text" )

HERMES_EXECUTABLE UINT8 HermesMeasureCacheLineData[ ] =
{ 
	0x49, 0x89, 0xC9, // mov r9, rcx
	0x0F, 0x31,       // rdtsc
	0x49, 0x89, 0xC0, // mov r8, rax
	0x45, 0x8A, 0x09, // mov r9b, byte ptr[r9]
	0x0F, 0x01, 0xF9, // rdtscp
	0x4C, 0x29, 0xC0, // sub rax, r8
	0xC3              // ret
};

#pragma code_seg( pop )

//
// Time a load operation from a specified cache line using the TSC.
//
UINT32( *HermesMeasureCacheLine )( 
	_In_ LPVOID CacheLine 
	) = NULL;

//
// Initialize or obtain a memory region whose cache lines will be 
// used as communication channel. Additionally, probe CPU features
// to determine if Hermes is supported on the running system.
//
BOOLEAN
HermesInitialize( 
	_In_ LPVOID CacheLines 
	)
{
	INT32 CpuidRegs[ 4 ]{ };

	//
	// CPUID EAX=7, ECX=0, extended features
	//
	__cpuidex( CpuidRegs, 7, 0 );

	//
	// EBX bit 23, CLFLUSHOPT instruction support
	//
	if ( ( ( CpuidRegs[ 1 ] >> 23 ) & 1 ) == FALSE )
	{
		//
		// CLFLUSHOPT is not supported on the current system.
		// 
		return FALSE;
	}

	if ( HermesLineSize == 0 )
	{
		//
		// CPUID EAX=1 ECX=0, feature bits and processor information
		//
		__cpuidex( CpuidRegs, 1, 0 );

		//
		// EBX bits 15:8, CLFLUSH line size. This line size should be 
		// multiplied by 8 to obtain the line size in bytes.
		//
		HermesLineSize = ( ( CpuidRegs[ 1 ] >> 8 ) & 0xFF ) * 8;
	}

	if ( HermesCacheLines == NULL )
	{
		if ( CacheLines != NULL )
		{
			//
			// The caller specified a preferred cache line region
			//
			HermesCacheLines = ( UINT8* )CacheLines;
		}
		else
		{
			//
			// Use ntdll.dll as default communication cache line region.
			//
			HermesCacheLines = ( UINT8* )GetModuleHandleA( "kernelbase.dll" );
		}
	}
	
	//
	// Use the Intel specific cahce line measurement routine when on Intel systems
	//
	HermesMeasureCacheLine = ( decltype( HermesMeasureCacheLine ) )&HermesMeasureCacheLineData;

	return TRUE;
}

#pragma optimize( push )
#pragma optimize( "", off )

/**
 * @brief Flush the communcation region's cache lines that correspond to set bits within a given bitmap
 * 
 * @param [in]  Bitmap: A bitmap whose bit positions correspond to the cache lines to flush
 * @param [in] NumBits: The number of bits in the bitmap
 */
VOID
HermesSetLines( 
	_In_ UINT64* Bitmap, 
	_In_ UINT32  NumBits 
	)
{
	CONST UINT64 LineSize = HermesLineSize;

	for ( UINT32 i = 0; i < NumBits; i++ )
	{
		if ( Bitmap[ i / 64 ] & ( 1ull << ( i % 64 ) ) )
		{
			//
			// Flush the cache line that corresponds to the current set bit
			//
			_mm_clflushopt( HermesCacheLines + ( i * LineSize ) );
		}
	}
}

/**
 * @brief Measure the average access time for a given number of cache lines within the commuincation region
 * 
 * @param [in] BaseAddress: The base address of the cache lines for which to obtain the average
 * @param [in]    NumLines: The number of cache lines to measure
 * @param [in]  NumSamples: The number of samples to obtain the average from
 * @param [in]     Results: A pointer to a list of UINT64s that will receive the average per line
 */
VOID
HermesMeasureLineAverage(
	_In_  LPVOID  BaseAddress,
	_In_  UINT32  NumLines,
	_In_  UINT32  NumSamples,
	_Out_ PUINT64 Results
	)
{
	CONST UINT64 LineSize = HermesLineSize;

	RtlZeroMemory( Results, NumLines * sizeof( UINT64 ) );

	for ( UINT32 i = NumSamples; i--; )
	{
		for ( UINT32 j = 0; j < NumLines; j++ )
		{
			Results[ j ] += HermesMeasureCacheLine( ( UINT8* )BaseAddress + ( j * LineSize ) );
		}
	}

	for ( UINT32 i = 0; i < NumLines; i++ )
	{
		Results[ i ] = Results[ i ] / NumSamples;
	}
}

/**
 * @brief Create a CRC32-C checksum for a given transmit block.
 * 
 * @param [in] Block: The block for which to create the checksum
 * 
 * @return The CRC32-C checksum
 */
UINT64
HermesCreateChecksum( 
	_In_ PHERMES_TRANSMIT_BLOCK Block
	)
{
	UINT64 Result = 0;
	UINT32 Crc    = ~1ul;

	for ( UINT32 i = 0; i < 4; i++ ) {
		Crc = _mm_crc32_u32( Crc, ( ( UINT32* )Block->Data )[ i ] );
	}

	Crc    = _mm_crc32_u32( Crc, Block->Length );
	Result = _mm_crc32_u32( Crc, Block->Position ) ^ ~1ul;
	Result = ( Result << 32ull ) 
		^ Block->Length 
		^ Block->Position 
		^ Block->Data[ 0 ] & 0xFFFFFFFF;

	return Result;
}

UINT64
HermesLinesToUint64( 
	VOID 
	)
{	
	CONST UINT64 NumBits = 64;

	UINT64 Average   [ NumBits ]{},
		   Likelihood[ NumBits ]{},
		   Result = 0;

	UINT32  NumSamples = 16;
	while ( NumSamples > 0 )
	{
		HermesMeasureLineAverage( HermesCacheLines, 64, 10, Average );

		for ( UINT32 i = 0; i < NumBits; i++ )
		{
			if ( Average[ i ] > 250 )
				Likelihood[ i ]++;
		}

		NumSamples--;
	}

	for ( UINT32 i = 0; i < NumBits; i++ )
	{
		BOOLEAN Bit = ( Likelihood[ i ] > ( NumSamples / 2 ) );

		Result |= ( UINT64 )Bit << i;
	}

	return Result;
}

VOID
HermesLinesToBlock( 
	_Out_ PHERMES_TRANSMIT_BLOCK Block
	)
{
	if ( Block == NULL )
	{
		return;
	}

	RtlZeroMemory( Block, sizeof( HERMES_TRANSMIT_BLOCK ) );

	CONST UINT64 NumBits = sizeof( HERMES_TRANSMIT_BLOCK ) * 8;

	UINT64 Average   [ NumBits ]{},
		   Likelihood[ NumBits ]{};

	UINT32  NumSamples = 16;
	while ( NumSamples > 0 )
	{
		for ( UINT32 i = 0; i < NumBits; i += 32 )
		{
			HermesMeasureLineAverage( HermesCacheLines + ( i * HermesLineSize ), 32, 10, &Average[ i ] );
		}

		for ( UINT32 i = 0; i < NumBits; i++ )
		{
			if ( Average[ i ] > 250 )
				Likelihood[ i ]++;
		}

		NumSamples--;
	}

	for ( UINT32 i = 0; i < NumBits; i++ )
	{
		BOOLEAN Bit = ( Likelihood[ i ] > ( NumSamples / 2 ) );

		Block->AsUInt[ i / 64 ] |= ( UINT64 )Bit << ( i % 64 );
	}
}

VOID
HermesBroadcastTransmitBlock( 
	_In_ PHERMES_TRANSMIT_BLOCK Block 
	)
{
	UINT64 FlushCount = HERMES_TRANSMIT_FLUSH_COUNT;

	while ( FlushCount > 0 )
	{
		HermesSetLines( Block->AsUInt, sizeof( HERMES_TRANSMIT_BLOCK ) * 8 );

		FlushCount--;
	}
}

#include <cstdio>

BOOLEAN
HermesSendReliableTransmitBlock( 
	_In_ PHERMES_TRANSMIT_BLOCK Block 
	)
{
	UINT64                Timeout = HERMES_TRANSMIT_TIMEOUT;
	HERMES_TRANSMIT_BLOCK OurBlock{ };

	while ( Timeout > 0 )
	{
		HermesBroadcastTransmitBlock( Block );
		HermesLinesToBlock( &OurBlock );

		if ( OurBlock.Acknowledgement == Block->Checksum )
		{
			return TRUE;
		}

		Timeout--;
	}

	return FALSE;
}

BOOLEAN
HermesReceiveReliableTransmitBlock( 
	_Out_ PHERMES_TRANSMIT_BLOCK RecvBlock 
	)
{
	UINT64                Timeout = HERMES_TRANSMIT_TIMEOUT;
	HERMES_TRANSMIT_BLOCK Block{ };

	while ( Timeout > 0 )
	{
		//
		// Obtain a transmit block from the cache lines
		//
		HermesLinesToBlock( &Block );

		if ( Block.Checksum != HermesCreateChecksum( &Block ) )
		{
			//
			// Continue attempting to read a transmit block whose
			// hash is correct, uness we reached timeout.
			//
			Timeout--;

			continue;
		}

		//
		// Set the acknowledgement field in the transmit block,
		// when the sender reads this field from it. It will
		// move onto the next block if there is one in the queue.
		//
		Block.Acknowledgement = Block.Checksum;

		//
		// Store the received transmit block for further processing
		//
		RtlCopyMemory( RecvBlock, &Block, sizeof( HERMES_TRANSMIT_BLOCK ) );

		//
		// Broadcast the block along with the acknowledgement
		// back to the sender.
		//
		HermesBroadcastTransmitBlock( &Block );

		return TRUE;
	}

	return FALSE;
}

BOOLEAN
HermesSendTransmissionEvent( 
	_In_ BOOLEAN StartOrEnd
	)
{
	UINT64 Timeout = HERMES_TRANSMIT_TIMEOUT;

	HERMES_TRANSMIT_BLOCK Block   { },
		                  CurBlock{ };

	if ( StartOrEnd == TRUE )
	{
		Block.Data[ 0 ] = HermesTransmitStartMagic.m128i_u64[ 0 ];
		Block.Data[ 1 ] = HermesTransmitStartMagic.m128i_u64[ 1 ];
	}
	else
	{
		Block.Data[ 0 ] = HermesTransmitEndMagic.m128i_u64[ 0 ];
		Block.Data[ 1 ] = HermesTransmitEndMagic.m128i_u64[ 1 ];
	}

	Block.Length    = 16;
	Block.Checksum  = HermesCreateChecksum( &Block );

	while ( Timeout > 0 )
	{
		//
		// Broadcast our transmission magic
		//
		HermesBroadcastTransmitBlock( &Block );

		//
		// Read the current transmission block to see if ackmnowledgement
		// cache lines are set.
		//
		HermesLinesToBlock( &CurBlock );

		if ( CurBlock.Acknowledgement == Block.Checksum )
		{
			//
			// Indicate the transmission event was successfully acknowledged 
			//
			return TRUE;
		}

		Timeout--;
	}

	return FALSE;
}

BOOLEAN
HermesGetTransmissionEvent( 
	_In_  PHERMES_TRANSMIT_BLOCK Block,
	_Out_ PBOOLEAN               StartOrEnd 
	)
{
	if ( Block->Data[ 0 ] == HermesTransmitStartMagic.m128i_u64[ 0 ] &&
		 Block->Data[ 1 ] == HermesTransmitStartMagic.m128i_u64[ 1 ] )
	{
		*StartOrEnd = TRUE;

		return TRUE;
	}

	if ( Block->Data[ 0 ] == HermesTransmitEndMagic.m128i_u64[ 0 ] &&
		 Block->Data[ 1 ] == HermesTransmitEndMagic.m128i_u64[ 1 ] )
	{
		*StartOrEnd = FALSE;

		return TRUE;
	}

	return FALSE;
}

BOOLEAN
HermesSendData( 
	_In_ LPVOID Data, 
	_In_ SIZE_T DataLength 
	)
{
	SIZE_T CurLength       = 0,
		   CurBlockNum     = 0,
		   BlockDataLength = sizeof( HERMES_TRANSMIT_BLOCK::Data );

	HERMES_TRANSMIT_BLOCK CurBlock{ };

	if ( HermesSendTransmissionEvent( TRUE ) == FALSE )
	{
		//
		// Transmission event was not received, and resulted in a timeout
		//
		return FALSE;
	}
	
	SIZE_T AlignedLength   = DataLength & ~( BlockDataLength - 1 ),
		   RemainingLength = DataLength &  ( BlockDataLength - 1 );

	while ( CurLength < AlignedLength || RemainingLength != 0 )
	{
		CurBlock.Length = BlockDataLength;

		if ( CurLength >= AlignedLength )
		{
			CurBlock.Length = RemainingLength;
			RemainingLength = 0;
		}

		CurBlock.Position = CurBlockNum;
			
		RtlCopyMemory( CurBlock.Data, ( UINT8* )Data + CurLength, CurBlock.Length );

		CurBlock.Checksum = HermesCreateChecksum( &CurBlock );

		if ( HermesSendReliableTransmitBlock( &CurBlock ) == FALSE )
		{
			//
			// Transmission block was not received, and resulted in a timeout
			//
			return FALSE;
		}

		CurBlockNum += 1;
		CurLength   += BlockDataLength;
	}

	if ( HermesSendTransmissionEvent( FALSE ) == FALSE )
	{
		//
		// Transmission event not received, and resulted in a timeout
		//
		return FALSE;
	}

	return TRUE;
}

BOOLEAN
HermesReceiveData( 
	_Out_ LPVOID Data, 
	_In_  SIZE_T BufferLength
	)
{
	RtlZeroMemory( Data, BufferLength );

	HERMES_TRANSMIT_BLOCK Block{ };
	BOOLEAN               TransmissionState = FALSE;
	
	HermesReceiveReliableTransmitBlock( &Block );

	if ( HermesGetTransmissionEvent( &Block, &TransmissionState ) == FALSE )
	{
		//
		// Transmission event was not received in time, return false for now.
		//
		return FALSE;
	}

	while ( TransmissionState == TRUE )
	{
		if ( HermesReceiveReliableTransmitBlock( &Block ) == TRUE )
		{
			//
			// Attempt to obtain a transmission event from the read block to see
			// if the transmission has ended.
			//
			if ( HermesGetTransmissionEvent( &Block, &TransmissionState ) == FALSE )
			{
				UINT8* Position = ( UINT8* )Data + ( Block.Position * sizeof( Block.Data ) ),
					 * Boundary = ( UINT8* )Data + BufferLength;

				if ( Position > Boundary )
				{
					//
					// Buffer is too small
					//
					return FALSE;
				}

				RtlCopyMemory( Position, Block.Data, Block.Length );
			}
		} 
		else return FALSE;
	}

	return TRUE;
}

#pragma optimize( pop )

#pragma warning( pop )
