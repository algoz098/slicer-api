import { HookContext } from '../declarations'

/**
 * Hook to extract file uploads from Koa context and attach to hook context
 */
export const extractFileUpload = () => {
  return async (context: HookContext) => {
    if (context.method !== 'create') {
      return context
    }

    // Ensure context.data is always an object
    if (!context.data || typeof context.data !== 'object') {
      context.data = {}
    }

    // Get the Koa context from params
    const koaCtx = (context.params as any)?.request?.ctx

    if (koaCtx?.request?.files?.file) {
      // Attach file to context data
      context.data = {
        ...context.data,
        uploadedFile: koaCtx.request.files.file
      }
    } else if (koaCtx?.feathersFiles?.file) {
      // Try alternative location
      context.data = {
        ...context.data,
        uploadedFile: koaCtx.feathersFiles.file
      }
    }

    return context
  }
}
