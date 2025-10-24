//
// File:
//     Hermes.h
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
#ifndef __HERMES_H__
#define __HERMES_H__

#include <Windows.h>
#include <intrin.h>

/**
 * @brief Initialize the Hermes covert timing channel functionality
 * 
 * @param [in] CacheLines: A pointer to a preferably page aligned virtual address
 *                         whose cache lines will be used to transmit data.
 * 
 * @return TRUE if the running system supports all architecture features required
 * @return FALSE if the running system does not support all architecture features required
 */
BOOLEAN
HermesInitialize( 
	_In_ LPVOID CacheLines 
	);

/**
 * @brief Attempt to establish a connection with the receiving process, and transmit
 *        a given buffer to it.
 * 
 * @param [in]       Data: A pointer to the data to send
 * @param [in] DataLength: The length of the data to send
 * 
 * @return TRUE if the data was transmitted successfully
 * @return FALSE if the data was not transmitted successfully
 */
BOOLEAN
HermesSendData( 
	_In_ LPVOID Data, 
	_In_ SIZE_T DataLength 
	);

/**
 * @brief Attempt to establish a connection with the sending process, and receive
 *        transmitted data within a given buffer.
 * 
 * @param [in]         Data: A pointer to a buffer that will receive the data
 * @param [in] BufferLength: The length of the buffer that will receive the data
 * 
 * @return TRUE if the data was received successfully
 * @return FALSE if the data was not received successfully
 */
BOOLEAN
HermesReceiveData( 
	_Out_ LPVOID Data, 
	_In_  SIZE_T BufferLength
	);

#endif
