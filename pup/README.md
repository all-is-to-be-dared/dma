Tools for uploading programs + tests

Basic workflow:
 1. Produce DUT.BIN file
 2. PUP sends DUT.BIN
 3. PUP_BOOT.IMG receives DUT.BIN, loads it into memory
 4. DUT.BIN starts and:
   1. sends a DUT_READY_SUITE message to PUP, and enters the SUITE workflow
   2. sends a DUT_READY_EXEC immediately runs to completion and resets
 5. (SUITE) PUP receives the BOOT_SYNCHRO, and begins to send test data to DUT.bin
 6. DUT.bin and PUP run the test suite, tabulate the results, and report

Protocol:
  0. PUP resets device
  1. PUP sends HOST_POLL
  2. BOOT sends ACK
Loop:
  4. BOOT sends DEV_RQCH
  5. PUP sends HOST_CHUNK (with same iden)
End
Loop:
 10. BOOT sends DEV_BEAT
End
 11. BOOT sends DEV_BOOT with success param
NOTE: it's currently (technically) possible for the PUP to miss the DEV_BOOT due to e.g. connection
      failure.

 13. DUT sends DUT_INIT
 14. DUT sends DUT_READY_SUITE or DUT_READY_EXEC
 15. PUP sends HOST_SUITE_ACK / HOST_EXEC_ACK

If SUITE:

Loop:
 14. DUT sends DUT_READY_TEST
 15. PUP sends HOST_TEST / HOST_SUITE_DONE
End
 16. DUT resets
 17. PUP exits

General wire protocol:

Message:
  \x01 (SOH)
  IDEN 2
  PLEN 2
  TYPE 4
  HDR_FCS16 2
  \x02 (STX)
  BODY PLEN
  BDY_FCS16 2
  \x03 (ETX)
  \x04 (EOT)
where IDEN is an auto-incrementing message ID, PLEN is the packet length

Ack:
  \x06 (ACK)
  IDEN 2
  FCS16 2
  \x04 (EOT)
where IDEN is the identifier of the message being Ack'd
