let silenceRefCount = 0

export async function withSilencedLogging(orca: any, fn: () => Promise<any>): Promise<any> {
  if (silenceRefCount === 0) {
    orca.setLoggingSilenced(true)
  }
  silenceRefCount++
  try {
    return await fn()
  } finally {
    silenceRefCount--
    if (silenceRefCount === 0) {
      orca.setLoggingSilenced(false)
    }
  }
}
