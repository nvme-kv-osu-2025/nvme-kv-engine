# NVMe KV Command Set Verification

## Objective

Verify whether the Samsung 990 PRO NVMe SSD supports Key-Value (KV) operations.

## Command Executed

```bash
sudo nvme show-regs /dev/nvme0 -H | grep -A1 "cap"
```

### Output

```
cap     : 800003028033fff
        Controller Ready With Media Support (CRWMS): Supported
--
pmrcap  : 0
        Controller Memory Space Supported (CMSS): Referencing PMR with host supplied addresses is Not Supported
```

## Analysis

Referencing the [NVM Express Base Specification 2.0e (2024.07.29 Ratified)](https://nvmexpress.org/wp-content/uploads/NVM-Express-Base-Specification-2.0e-2024.07.29-Ratified.pdf), specifically **Figure 38: Offset 0h: CAP - Controller Capabilities**:

For bits **44:37** of the CAP register, **bit 6** should be set as:

> "Controllers that support I/O Command Sets other than the NVM Command Set shall set this bit to '1'."

This bit is **not set**, which confirms the device does **not** support KV operations.

## Conclusion

The Samsung 990 PRO only implements the standard NVM (block I/O) Command Set. The NVMe KV Command Set is not supported by this hardware.
