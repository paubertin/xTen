#pragma once

#include "../graphics/API/context.h"
#include "inputmanager.h"
#include "window.h"
#include "../utils/utils.h"
#include "../graphics/API/atb.h"

namespace xten {

	class Client
	{
	public:
		Client();
		virtual ~Client();
		bool init(xgraphics::API::XTEN_GL_PROFILE gltype = xgraphics::API::XTEN_GL_ANY_PROFILE, xgraphics::API::XTEN_GL_FW_TYPE type = xgraphics::API::XTEN_FW_TYPE_GLFW);
		bool createWindow(const std::string& name, const WindowProperties& properties);
		virtual bool start();
		void run();
		void close() { m_Running = false; }
		virtual void onUpdate();
		virtual void onRender();
		void shutdown();
		const bool isRunning() const { return m_Running; }
		inline Window* const getWindow() const { return m_Window; }

	private:
		static Client* s_Instance;
		void setCallbacks();
		bool m_Running;
	protected:
		InputManager * m_Input;
		Window * m_Window;
		xgraphics::Camera* m_Camera;
		xmaths::Pipeline* m_Pipeline;
		xgraphics::API::XTEN_GL_FW_TYPE m_FwType;
		xgraphics::API::XTEN_GL_PROFILE m_GLProfile;
		xgraphics::API::ATB m_ATB;
	};

}