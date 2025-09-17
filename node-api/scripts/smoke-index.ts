import axios from 'axios'
import { app } from '../src/app'

async function main() {
  const server = await app.listen(0)
  try {
    const address = server.address()
    const port = typeof address === 'string' || address === null ? 0 : address.port
    const baseURL = `http://127.0.0.1:${port}`

    // Index page
    const { data } = await axios.get(baseURL)
    if (typeof data !== 'string' || data.indexOf('<html lang="en">') === -1) {
      console.error('Index não retornou HTML esperado')
      process.exit(2)
    }

    // 404 JSON
    try {
      await axios.get(`${baseURL}/path/to/nowhere`, { responseType: 'json' })
      console.error('404 esperado mas requisição teve sucesso')
      process.exit(3)
    } catch (err: any) {
      const resp = err?.response
      if (!resp || resp.status !== 404 || resp.data?.name !== 'NotFound') {
        console.error('JSON 404 inesperado:', resp?.status, resp?.data)
        process.exit(4)
      }
    }

    process.exit(0)
  } catch (err) {
    console.error('Falha no smoke-index:', err)
    process.exit(1)
  } finally {
    await app.teardown()
  }
}

main()

