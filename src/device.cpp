#include "device.h"
#include "json/cJSON.h"
#include "imgui.h"

#include <cstring>
#include <algorithm>

////////////////////////////////////////////////////////////////
///     EVENT & INPUT
////////////////////////////////////////////////////////////////

/// Structure containing the state of all inputs at a given time
struct InputState
{
	bool keyboard[K_ENDFLAG];   ///< Keyboard keys
	bool mouse[MB_ENDFLAG];     ///< Mouse buttons
	int  wheel;                 ///< Mouse wheel absolute pos

	bool close_signal;          ///< Window closing signal

	vec2i mouse_pos;            ///< Mouse absolute position
};


struct Listener
{
	Listener( ListenerFunc func, void *d ) : function( func ), data( d ) {}

	ListenerFunc    function;   ///< actual listener function
	void            *data;      ///< data to be set when registering listener
};



/// Manage real time events from GLFW callbacks
/// Distribute these events to registered listeners
class EventManager
{
public:
	EventManager();

	/// Called every frame to update the input & event states
	void Update();

	/// Adds a new listener of the given type to the event manager
	/// @return : true if the operation was successful
	bool AddListener( ListenerType type, ListenerFunc func, void *data );

	/// Sends an event to the connected listeners
	void PropagateEvent( const Event &evt );

	InputState				curr_state,         ///< Inputs of current frame
		prev_state;         ///< Inputs of previous frame

	std::vector<Listener>   key_listeners,      ///< List of all registered mouse listeners
		mouse_listeners,    ///< List of all registered key listeners
		resize_listeners,   ///< List of all registered resize listeners
		scroll_listeners;

	std::vector<Event>      frame_key_events,    ///< All key events recorded during the frame
		frame_mouse_events,  ///< All mouse events recorded during the frame
		frame_resize_events; ///< All resize events recorded during the frame

};

static EventManager *em = NULL;


EventManager::EventManager()
{
	// Init states
	memset( curr_state.keyboard, false, K_ENDFLAG * sizeof( bool ) );
	memset( curr_state.mouse, false, MB_ENDFLAG * sizeof( bool ) );
	curr_state.wheel = 0;
	curr_state.close_signal = false;

	memset( prev_state.keyboard, false, K_ENDFLAG * sizeof( bool ) );
	memset( prev_state.mouse, false, MB_ENDFLAG * sizeof( bool ) );
	prev_state.wheel = 0;
	prev_state.close_signal = false;

	// Init Listener arrays
	key_listeners.reserve( 10 );
	mouse_listeners.reserve( 10 );
	resize_listeners.reserve( 5 );

	frame_key_events.reserve( 50 );
	frame_mouse_events.reserve( 50 );
	frame_resize_events.reserve( 50 );

	LogInfo( "Event manager successfully initialized!" );
}


void EventManager::Update()
{
	// if there has been key events during the frame, send them to all keylisteners
	if ( frame_key_events.size() )
	{
		for ( u32 j = 0; j < frame_key_events.size(); ++j )
			for ( u32 i = 0; i < key_listeners.size(); ++i )
			{
				const Listener &ls = key_listeners[i];
				ls.function( frame_key_events[j], ls.data );
			}

		frame_key_events.clear();
	}
	// if there has been mouse events during the frame, send them to all mouselisteners
	if ( frame_mouse_events.size() )
	{
		for ( u32 j = 0; j < frame_mouse_events.size(); ++j )
			for ( u32 i = 0; i < mouse_listeners.size(); ++i )
			{
				const Listener &ls = mouse_listeners[i];
				ls.function( frame_mouse_events[j], ls.data );
			}

		frame_mouse_events.clear();
	}
	// if there has been resize events during the frame, send them to all resizelisteners
	if ( frame_resize_events.size() )
	{
		for ( u32 j = 0; j < frame_resize_events.size(); ++j )
			for ( u32 i = 0; i < resize_listeners.size(); ++i )
			{
				const Listener &ls = resize_listeners[i];
				ls.function( frame_resize_events[j], ls.data );
			}

		frame_resize_events.clear();
	}



	// Set previous state to current state.
	memcpy( prev_state.keyboard, curr_state.keyboard, K_ENDFLAG * sizeof( bool ) );
	memcpy( prev_state.mouse, curr_state.mouse, MB_ENDFLAG * sizeof( bool ) );

	prev_state.wheel = curr_state.wheel;
	prev_state.close_signal = curr_state.close_signal;
	prev_state.mouse_pos[0] = curr_state.mouse_pos[0];
	prev_state.mouse_pos[1] = curr_state.mouse_pos[1];
}

bool EventManager::AddListener( ListenerType type, ListenerFunc func, void *data )
{
	Listener s( func, data );

	// switch on Listener type
	switch ( type )
	{
	case LT_KeyListener:
		key_listeners.push_back( s );
		break;
	case LT_MouseListener:
		mouse_listeners.push_back( s );
		break;
	case LT_ResizeListener:
		resize_listeners.push_back( s );
		break;
	default:
		return false;
	}


	return true;
}

void EventManager::PropagateEvent( const Event &evt )
{
	switch ( evt.type )
	{
	case EKeyPressed:
	case EKeyReleased:
	case ECharPressed:
		frame_key_events.push_back( evt );
		break;
	case EMouseMoved:
	case EMousePressed:
	case EMouseReleased:
	case EMouseWheelMoved:
		frame_mouse_events.push_back( evt );
		break;
	case EWindowResized:
		frame_resize_events.push_back( evt );
		break;
	default:
		break;
	}
}

u32  Device::GetMouseX() const
{
	return em->curr_state.mouse_pos[0];
}

u32  Device::GetMouseY() const
{
	return em->curr_state.mouse_pos[1];
}


bool Device::IsKeyDown( Key pK ) const
{
	return em->curr_state.keyboard[pK];
}

bool Device::IsKeyUp( Key pK ) const
{
	return !em->curr_state.keyboard[pK] && em->prev_state.keyboard[pK];
}

bool Device::IsKeyHit( Key pK ) const
{
	return em->curr_state.keyboard[pK] && !em->prev_state.keyboard[pK];
}


bool Device::IsMouseDown( MouseButton pK ) const
{
	return em->curr_state.mouse[pK];
}

bool Device::IsMouseUp( MouseButton pK ) const
{
	return !em->curr_state.mouse[pK] && em->prev_state.mouse[pK];
}

bool Device::IsMouseHit( MouseButton pK ) const
{
	return em->curr_state.mouse[pK] && !em->prev_state.mouse[pK];
}


bool Device::IsWheelUp() const
{
	return em->curr_state.wheel > em->prev_state.wheel;
}

bool Device::IsWheelDown() const
{
	return em->curr_state.wheel < em->prev_state.wheel;
}

// GLFW Event Callback functions
// TODO : block events from being propagated to other listeners in some cases
//		  example : when using inputs on GUI, they aren't used on the game itself(the scene)
static void KeyPressedCallback( GLFWwindow *win, int key, int scancode, int action, int mods )
{
	bool pressed = ( action == GLFW_PRESS || action == GLFW_REPEAT );

	em->curr_state.keyboard[key] = pressed ? true : false;

	Event e;
	e.type = ( pressed ? EKeyPressed : EKeyReleased );
	e.i = key;
	e.key = (Key) key;

	em->PropagateEvent( e );
}

static void CharPressedCallback( GLFWwindow *win, unsigned int c )
{
	Event e;
	e.type = ECharPressed;
	e.i = c;

	em->PropagateEvent( e );
}

static void MouseButtonCallback( GLFWwindow *win, int button, int action, int mods )
{
	bool pressed = action == GLFW_PRESS;
	em->curr_state.mouse[button] = pressed;

	Event e;
	e.type = pressed ? EMousePressed : EMouseReleased;
	e.v = em->curr_state.mouse_pos;
	e.button = (MouseButton) button;

	em->PropagateEvent( e );
}

static void MouseWheelCallback( GLFWwindow *win, double off_x, double off_y )
{
	em->curr_state.wheel += (int) off_y;

	Event e;
	e.type = EMouseWheelMoved;
	e.i = ( em->curr_state.wheel - em->prev_state.wheel );

	em->PropagateEvent( e );
}

static void MouseMovedCallback( GLFWwindow *win, double x, double y )
{
	em->curr_state.mouse_pos = vec2i( (int) x, (int) y );

	Event e;
	e.type = EMouseMoved;
	e.v = vec2i( (int) x, (int) y );

	em->PropagateEvent( e );
}

static void WindowResizeCallback( GLFWwindow *win, int width, int height )
{
	Event e;
	e.type = EWindowResized;
	e.v = vec2i( width, height );

	em->PropagateEvent( e );
}

static void ErrorCallback( int error, char const *desc )
{
	LogErr( desc );
}

//////////////////////
static bool LoadConfig( Config &config )
{
	Json conf_file;
	if ( !conf_file.Open( "config.json" ) )
	{
		return false;
	}

	config.windowSize.x = Json::ReadInt( conf_file.root, "iWindowWidth", 1024 );
	config.windowSize.y = Json::ReadInt( conf_file.root, "iWindowHeight", 768 );
	config.MSAASamples = Json::ReadInt( conf_file.root, "iMSAA", 0 );
	config.fullscreen = Json::ReadInt( conf_file.root, "bFullScreen", 0 ) != 0;
	config.vSync = Json::ReadInt( conf_file.root, "bVSync", 0 ) != 0;
	config.fov = Json::ReadFloat( conf_file.root, "fFOV", 75.f );
	config.anisotropicFiltering = Json::ReadInt( conf_file.root, "iAnisotropicFiltering", 0 );
	config.cameraBaseSpeed = Json::ReadFloat( conf_file.root, "fCameraSpeedBase", 10.f );
	config.cameraSpeedMult = Json::ReadFloat( conf_file.root, "fCameraSpeedMult", 2.f );
	config.cameraRotationSpeed = Json::ReadFloat( conf_file.root, "fCameraRotationSpeed", 1.f );
	config.cameraPosition = Json::ReadVec3( conf_file.root, "vCameraPosition", vec3f( 10, 8, 10 ) );
	config.cameraTarget = Json::ReadVec3( conf_file.root, "vCameraTarget", vec3f( 0, 0.5, 0 ) );

	conf_file.Close();
	return true;
}
//////////////////////

static void DeviceResizeEventListener( const Event &event, void *data )
{
	Device *d = static_cast<Device*>( data );
	d->windowSize[0] = event.v[0];
	d->windowSize[1] = event.v[1];
	d->windowCenter[0] = event.v[0] / 2;
	d->windowCenter[1] = event.v[1] / 2;

	d->UpdateProjection();
}

Device *gDevice = NULL;

Device &GetDevice()
{
	if ( !gDevice )
	{
		gDevice = new Device();
	}
	return *gDevice;
}

void DestroyDevice()
{
	if ( gDevice )
	{
		gDevice->Destroy();
		delete gDevice;
		gDevice = nullptr;
	}
}

bool Device::Init( LoopFunction loopFunction )
{
	int v;
	mainLoop = loopFunction;

	Random::InitRandom();

	// Open and parse config file
	if ( !LoadConfig( config ) )
	{
		LogErr( "Error loading config file." );
		return false;
	}

	windowSize = config.windowSize;
	windowCenter = windowSize / 2;
	fov = config.fov;
	mouseLastPosition = windowCenter;
	mousePosition = windowCenter;

	// Initialize GLFW and callbacks
	if ( !glfwInit() )
	{
		LogErr( "Error initializing GLFW." );
		return false;
	}

	glfwWindowHint( GLFW_SAMPLES, config.MSAASamples );

	std::stringstream window_name;
	window_name << "Radar v" << RADAR_MAJOR << "." << RADAR_MINOR << "." << RADAR_PATCH;
	window = glfwCreateWindow( windowSize.x, windowSize.y, window_name.str().c_str(),
		config.fullscreen ? glfwGetPrimaryMonitor() : NULL, NULL );
	if ( !window )
	{
		LogErr( "Error initializing GLFW window." );
		glfwTerminate();
		return false;
	}

	glfwSwapInterval( (int) config.vSync );

	if ( !config.fullscreen )
		glfwSetWindowPos( gDevice->window, 100, 100 );

	glfwMakeContextCurrent( gDevice->window );
	glfwSetKeyCallback( gDevice->window, KeyPressedCallback );
	glfwSetCharCallback( gDevice->window, CharPressedCallback );
	glfwSetMouseButtonCallback( gDevice->window, MouseButtonCallback );
	glfwSetCursorPosCallback( gDevice->window, MouseMovedCallback );
	glfwSetScrollCallback( gDevice->window, MouseWheelCallback );
	glfwSetWindowSizeCallback( gDevice->window, WindowResizeCallback );
	glfwSetErrorCallback( ErrorCallback );

	LogInfo( "GLFW successfully initialized." );

	if ( GLEW_OK != glewInit() )
	{
		LogErr( "Error initializing GLEW." );
		glfwDestroyWindow( window );
		glfwTerminate();
		return false;
	}

	LogInfo( "GLEW successfully initialized." );

	GLubyte const *renderer = glGetString( GL_RENDERER );
	GLubyte const *version = glGetString( GL_VERSION );
	LogInfo( "Renderer: ", renderer );
	LogInfo( "GL Version: ", version );

	v = 0;
	glGetIntegerv( GL_MAX_VERTEX_ATTRIBS, &v );
	if ( v < SHADER_MAX_ATTRIBUTES )
	{
		LogErr( "Your Graphics Card must support at least ", SHADER_MAX_ATTRIBUTES,
			" vertex attributes. It can only ", v, "." );
		glfwDestroyWindow( window );
		glfwTerminate();
		return false;
	}

	glGetIntegerv( GL_MAX_VERTEX_UNIFORM_COMPONENTS, &v );
	if ( v < SHADER_MAX_UNIFORMS )
	{
		LogErr( "Your Graphics Card must support at least ", 16 * SHADER_MAX_UNIFORMS,
			" vertex uniform components. It can only ", v, "." );
		glfwDestroyWindow( window );
		glfwTerminate();
		return false;
	}

	LogInfo( "Maximum Vertex Uniforms: ", v );

	glGetIntegerv( GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, &v );
	if ( v < SHADER_MAX_UNIFORMS )
	{
		LogErr( "Your Graphics Card must support at least ", 16 * SHADER_MAX_UNIFORMS,
			" fragment uniform components. It can only ", v, "." );
		glfwDestroyWindow( window );
		glfwTerminate();
		return false;
	}

	LogInfo( "Maximum Fragment Uniforms: ", v );

	f32 largest_aniso = 0;
	glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largest_aniso );
	LogInfo( "Max Anisotropic Filtering : ", (int) largest_aniso );

	config.anisotropicFiltering = std::min( config.anisotropicFiltering, (u32) largest_aniso );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, (GLfloat) config.anisotropicFiltering );

	// Set GL States
	glEnable( GL_CULL_FACE );
	glCullFace( GL_BACK );
	glFrontFace( GL_CCW );

	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LESS );

	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	glClearColor( 0.f, 0.f, 0.f, 0.f );

	// Initialize EventManager
	if ( !em )
	{
		em = new EventManager();
	}

	if ( !ImGui_Init() )
	{
		LogErr( "Error initializing ImGUI." );
		glfwDestroyWindow( window );
		glfwTerminate();
		return false;
	}
	em->AddListener( LT_KeyListener, ImGui_KeyListener, nullptr );
	em->AddListener( LT_MouseListener, ImGui_MouseListener, nullptr );
	LogInfo( "ImGUI successfully initialized." );

	if ( !Render::Init() )
	{
		LogErr( "Error initializing Device's Renderer." );
		goto err;
	}

	// Initialize projection matrix
	UpdateProjection();

	engineTime = 0.0;

	// Initialize listeners in order
	if ( !AddEventListener( LT_ResizeListener, SceneResizeEventListener, this ) )
	{
		LogErr( "Error registering scene as a resize event listener." );
		goto err;
	}
	if ( !AddEventListener( LT_ResizeListener, DeviceResizeEventListener, this ) )
	{
		LogErr( "Error registering device as a resize event listener." );
		goto err;
	}

	LogInfo( "Device successfully initialized." );

	return true;


err:
	delete em; em = nullptr;
	glfwDestroyWindow( window );
	window = nullptr;
	glfwTerminate();
	return false;
}

void Device::Destroy()
{
	ImGui_Destroy();
	if ( em ) delete em;
	Render::Destroy();

	if ( window )
	{
		glfwDestroyWindow( window );
		glfwTerminate();
	}
}

bool Device::AddEventListener( ListenerType type, ListenerFunc func, void *data )
{
	return em->AddListener( type, func, data );
}

void Device::SetMouseX( int x ) const
{
	x = std::min( x, windowSize.x - 1 );
	x = std::max( x, 0 );
	em->curr_state.mouse_pos.x = x;
	glfwSetCursorPos( window, em->curr_state.mouse_pos.x, em->curr_state.mouse_pos.y );
}

void Device::SetMouseY( int y ) const
{
	y = std::min( y, windowSize.y - 1 );
	y = std::max( y, 0 );
	em->curr_state.mouse_pos.y = y;
	glfwSetCursorPos( window, em->curr_state.mouse_pos.x, em->curr_state.mouse_pos.y );
}

void Device::ShowCursor( bool flag ) const
{
	glfwSetInputMode( window, GLFW_CURSOR, flag ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN );
}

void Device::UpdateProjection()
{
	glViewport( 0, 0, windowSize.x, windowSize.y );
	projection_matrix_3d = mat4f::Perspective( fov, windowSize[0] / (f32) windowSize[1],
		.1f, 1000.f );

	projection_matrix_2d = mat4f::Ortho(0, (f32)windowSize.x,
					    (f32)windowSize.y, 0,
					    0.f, 100.f);

	Render::UpdateProjectionMatrix3D( projection_matrix_3d );
	Render::UpdateProjectionMatrix2D( projection_matrix_2d );
}



void Device::Run()
{
	// Update Projection once for all user created shaders
	UpdateProjection();

    int gameRefreshHz = 60;
    f32 targetSecondsPerFrame = 1.f / gameRefreshHz;

	f64 dt, t, last_t = glfwGetTime();

	while ( !glfwWindowShouldClose( window ) )
	{
		glfwPollEvents();
		ImGui_NewFrame();

		// Time management
		t = glfwGetTime();
		dt = t - last_t;

        if (dt < targetSecondsPerFrame)
        {
            while (dt < targetSecondsPerFrame)
            {
                t = glfwGetTime();
                dt = t - last_t;
            }
        }
        else
        {
            LogInfo("Missed frame rate!");
        }

		last_t = t;
		engineTime += dt;

		// Keyboard inputs for Device
		if ( IsKeyUp( K_Escape ) )
			glfwSetWindowShouldClose( window, GL_TRUE );

		// register mouse position
		mouseLastPosition = em->prev_state.mouse_pos;
		mousePosition = em->curr_state.mouse_pos;

		mainLoop( (f32) dt );

		ImGui::Render(); // ADRIEN - should that be here or in the custom loop function

		glfwSwapBuffers( window );
		em->Update();
	}
}
