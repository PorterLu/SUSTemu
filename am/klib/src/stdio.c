#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdarg.h>
#include <stdint.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

/*
int int2str(int num, char *str);
int print_template(char *buf, int num, const char* fmt, ...){
	int i,j,index = 0;
	char out[1024];
	va_list var_arg;
	va_start(var_arg, fmt);
	
	size_t len = strlen(fmt);
	for(i = 0; i < len; i++)
	{

		if(fmt[i] == '%' && (i + 1) < strlen(fmt))
		{
			if(fmt[i+1] == 'd')
			{
				i++;
				int tmp1 = va_arg(var_arg, int);
				char tmp2[500];
				int len = int2str(tmp1, tmp2);
				for(j=0; j<len; j++)
				{
					out[index] = tmp2[j];
					index++;
				}
			}
			else if(fmt[i+1] == 's')
			{
				i++;
				char* tmp = va_arg(var_arg, char*);
				j = 0;
				while(tmp[j] != '\0')
				{
					out[index] = tmp[j];
					j++;
					index++;
				}					
			}
			else if(fmt[i+1] == '%')
			{
				i++;
				out[index++] = '%';
			}
			else
			{
				return -1;
			}
		}
		else
		{
			out[index] = fmt[i];
			index++;
		}
	}

	va_end(var_arg);
	out[index] = '\0';
	if(num == 0)
	{
		for(int i=0; i<index; i++)
			putch(out[i]);
	}else if(num == 1){
		for(int i=0; i<=index; i++)
			buf[i] = out[i];
	}

	return index;
}
*/

//only for positive number
int int2str(int num, char *str)
{
	bool isNeg = false;
	int appendix = 1;
	int index = 0,i;
	if(num == 0)
	{
		str[index] = '0';
		index++;
	}

	if(num<0)
	{
		str[index] = '-';
		isNeg = true;
		index++;
		appendix = 0;
	}
	
	while(num != 0)
	{
		str[index] = (isNeg? (-(num%10)):num%10)+ 0x30;
		index ++;
		num = num / 10;
	}

	for(i=isNeg; i < (index + isNeg)/2; i++)
	{
		int tmp = str[index - i - appendix];
		str[index - i - appendix ] = str[i];
		str[i] = tmp;
	}

	str[index] = '\0';
	return index;
}


int vsprintf(char *out, const char *fmt, va_list ap) {
  panic("Not implemented");
}


int printf(const char* fmt, ...){
	//char out[65536];
	int i,j,index = 0;
	va_list var_arg;
	va_start(var_arg, fmt);
	
	size_t len = strlen(fmt);
	for(i = 0; i < len; i++)
	{

		if(fmt[i] == '%' && (i + 1) < strlen(fmt))
		{
			if(fmt[i+1] == 'd')
			{
				i++;
				int tmp1 = va_arg(var_arg, int);
				char tmp2[500];
				int len = int2str(tmp1, tmp2);
				for(j=0; j<len; j++)
				{
					//out[index] = tmp2[j];
					putch(tmp2[j]);
					index++;
				}
			}
			else if(fmt[i+1] == 's')
			{
				i++;
				char* tmp = va_arg(var_arg, char*);
				j = 0;
				while(tmp[j] != '\0')
				{
					//out[index] = tmp[j];
					putch(tmp[j]);
					j++;
					index++;
				}					
			}
			else if(fmt[i+1] == '%')
			{
				i++;
				//out[index++] = '%';
				putch('%');
			}
			else
			{
				return -1;
			}
		}
		else
		{
			//out[index] = fmt[i];
			putch(fmt[i]);
			index++;
		}
	}

	va_end(var_arg);
	/*out[index] = '\0';
	
	for(int i=0; i<index; i++)
		putch(out[i]);*/
	return index;
}

int sprintf(char *out, const char *fmt, ...) {
	int i,j,index = 0;
	va_list var_arg;
	va_start(var_arg, fmt);
	
	//if(strlen(fmt) == 2)
	//	asm("ebreak");
	size_t len = strlen(fmt);
	for(i = 0; i < len; i++)
	{

		if(fmt[i] == '%' && (i + 1) < strlen(fmt))
		{
			if(fmt[i+1] == 'd')
			{
				i++;
				int tmp1 = va_arg(var_arg, int);
				char tmp2[500];
				int len = int2str(tmp1, tmp2);
				for(j=0; j<len; j++)
				{
					out[index] = tmp2[j];
					index++;
				}
			}
			else if(fmt[i+1] == 's')
			{
				i++;
				char* tmp = va_arg(var_arg, char*);
				j = 0;
				while(tmp[j] != '\0')
				{
					out[index] = tmp[j];
					j++;
					index++;
				}					
			}
			else if(fmt[i+1] == '%')
			{
				i++;
				out[index++] = '%';
			}
			else
			{
				return -1;
			}
		}
		else
		{
			out[index] = fmt[i];
			index++;
		}
	}

	va_end(var_arg);
	out[index] = '\0';
	return index;
}

int snprintf(char *out, size_t n, const char *fmt, ...) {
  panic("Not implemented");
}

int vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
  panic("Not implemented");
}

#endif
